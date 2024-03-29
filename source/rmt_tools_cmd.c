/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <ctype.h>
#include "esp_log.h"

#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include <nvs_flash.h>
#include "esp_netif.h"
#include "esp_http_server.h"
#include "driver/gpio.h"

#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#include "jsmn.h"
#include "rmt_tools.h"

static const char *TAG = "rmt_tools_cmd";

#define RMT_TIMEOUT_MS (10000)
// web cfg transmit
#define RMT_GPIO_OUT "RMTGPIOOut"
#define RMT_CLK_OUT "RMTClkOut"
#define RMT_LOOP_OUT "RMTLoopOut"
#define RMT_TRIG_OUT "RMTTrigGPIO"
#define RMT_NS_MKS_MS "RMTOutnSmkSMs"
#define RMT_WRITE_DATA_OUT "RMTWriteData"
// cmd
#define RMT_TRANSMIT_CMD "RMTStartOut"
// web cfg receive
#define RMT_GPIO_IN "RMTGPIOIn"
#define RMT_CLK_IN "RMTClkIn"
#define RMT_EOF_MARKER "RMTEOFMarker"
#define RMT_IN_OUT_SHORT "RMTInOutShort"
// cmd
#define RMT_RECEIVE_CMD "RMTStartIn"

typedef struct async_resp_arg
{
    httpd_handle_t hd;
    int fd;
} async_resp_arg_t;
static async_resp_arg_t ra;

typedef struct rmt_tools_cfg
{
    int gpio_out;               // rmt transmit gpio
    int clk_out;                // rmt transmit tick resolution hz
    int loop_out;               // rmt transmit loop count 0-> once, -1-> non stop
    int trig;                   // rmt transmi dbg gpio 1->start transmit , 0->stop transmit
    int rmt_ns_mks_ms;          // 1/1000/1000000 -> data out scale
    rmt_symbol_word_t rmt_data_out[64]; // rmt transmit data
    int data_out_len;           // calculated number of transmit samples

    int gpio_in;                // rmt receive gpio
    int clk_in;                 // rmt receive tick resolution hz
    int eof_marker;             // rmt recrive eof marker ( nSek )
    int in_out_short;           // rmt receive/transmit loopback
    rmt_symbol_word_t rmt_data_in[64]; // rmt receive data
    int data_in_len;

} rmt_tools_cfg_t;

static rmt_tools_cfg_t rmt_tools_cfg = {
    .clk_out = 1000000,
    .clk_in = 1000000,
    .rmt_ns_mks_ms = 1000,
    .eof_marker = 100000,
    .in_out_short = 1
    };

static  int rmt_transmit_started = 0;
static  int rmt_receive_started = 0;

// set/clear trigger pin
static void trig_set(int lvl)
{
    if (rmt_tools_cfg.trig != -1)
        gpio_set_level(rmt_tools_cfg.trig, lvl);
}

// simple json parse -> only one parametr name/val
static esp_err_t json_to_str_parm(char *jsonstr, char *nameStr, char *valStr) // распаковать строку json в пару  name/val
{
    int r; // количество токенов
    jsmn_parser p;
    jsmntok_t t[5]; // только 2 пары параметров и obj

    jsmn_init(&p);
    r = jsmn_parse(&p, jsonstr, strlen(jsonstr), t, sizeof(t) / sizeof(t[0]));
    if (r < 2)
    {
        valStr[0] = 0;
        nameStr[0] = 0;
        return ESP_FAIL;
    }
    strncpy(nameStr, jsonstr + t[2].start, t[2].end - t[2].start);
    nameStr[t[2].end - t[2].start] = 0;
    if (r > 3)
    {
        strncpy(valStr, jsonstr + t[4].start, t[4].end - t[4].start);
        valStr[t[4].end - t[4].start] = 0;
    }
    else
        valStr[0] = 0;
    return ESP_OK;
}
// send string to ws
static void send_string_to_ws(char *str)
{
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = (uint8_t *)str;
    ws_pkt.len = strlen(str);

    httpd_ws_send_frame_async(ra.hd, ra.fd, &ws_pkt);
    // httpd_ws_send_data(ra.hd, ra.fd, &ws_pkt);
}

static void send_default_rmt_tools_cfg_to_ws()
{
    char jsonstr[64] = {0};
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_GPIO_OUT, rmt_tools_cfg.gpio_out);
    send_string_to_ws(jsonstr);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_CLK_OUT, rmt_tools_cfg.clk_out);
    send_string_to_ws(jsonstr);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_NS_MKS_MS, rmt_tools_cfg.rmt_ns_mks_ms);
    send_string_to_ws(jsonstr);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_LOOP_OUT, rmt_tools_cfg.loop_out);
    send_string_to_ws(jsonstr);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_TRIG_OUT, rmt_tools_cfg.trig);
    send_string_to_ws(jsonstr);

    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_GPIO_IN, rmt_tools_cfg.gpio_in);
    send_string_to_ws(jsonstr);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_CLK_IN, rmt_tools_cfg.clk_in);
    send_string_to_ws(jsonstr);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_EOF_MARKER, rmt_tools_cfg.eof_marker);
    send_string_to_ws(jsonstr);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_IN_OUT_SHORT, rmt_tools_cfg.in_out_short);
    send_string_to_ws(jsonstr);
}

static bool rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    // send the received RMT symbols to the parser task
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    // return whether any task is woken up
    return high_task_wakeup == pdTRUE;
}

static QueueHandle_t receive_queue;

static void rmt_receive_tools(void *p)
{

    ESP_LOGI(TAG, "RMT Start receive");
    send_string_to_ws("RMT Start receive");

    rmt_channel_handle_t rx_chan = NULL;
    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // select source clock
        .resolution_hz = rmt_tools_cfg.clk_in, // tick resolution, 
        .mem_block_symbols = 64,               // memory block size, 64 * 4 = 256 Bytes
        .gpio_num = rmt_tools_cfg.gpio_in,     // GPIO number
        .flags.invert_in = false,              // do not invert input signal
        .flags.with_dma = false,               // do not need DMA backend
        .flags.io_loop_back = rmt_tools_cfg.in_out_short,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));
    receive_queue = xQueueCreate(10, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, receive_queue));
    ESP_ERROR_CHECK(rmt_enable(rx_chan));
    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 10,                       // the shortest duration
        .signal_range_max_ns = rmt_tools_cfg.eof_marker, // the longest duration
    };
    ESP_ERROR_CHECK(rmt_receive(rx_chan, rmt_tools_cfg.rmt_data_in, sizeof(rmt_tools_cfg.rmt_data_in), &receive_config));
    rmt_rx_done_event_data_t rx_data;

    if (xQueueReceive(receive_queue, &rx_data, RMT_TIMEOUT_MS / portTICK_PERIOD_MS) == pdFALSE)
    {
        ESP_LOGI(TAG, "RMT Recrive timeout 10 sec");
        send_string_to_ws("RMT Recrive timeout 10 sec");
    }
    else
    {
        for (int i = 0; i < rx_data.num_symbols; i++)
        {
            char *s0;
            int d0 = rx_data.received_symbols[i].duration0*(10000/(rmt_tools_cfg.clk_in/1000000));
            if (d0 < 10000) {s0 = "nS"; d0/=10;}
            else if ((d0 < 10000000)){s0 = "mkS"; d0/=10000;}
            else {s0 = "mS"; d0/=10000000;}
            char *s1;
            int d1 = rx_data.received_symbols[i].duration1*(10000/(rmt_tools_cfg.clk_in/1000000));
            if (d1 < 10000) {s1 = "nS";d1/=10;}
            else if ((d1 < 10000000)){s1 = "mkS"; d1/=10000;}
            else {s1 = "mS"; d1/=10000000;}

            char sendstr[64];
            sprintf(sendstr, "%d %d->%d %s %d->%d %s",i,rx_data.received_symbols[i].level0,d0,s0,rx_data.received_symbols[i].level1,d1,s1);
/*
            sprintf(sendstr, "%d %d->%d ns %d->%d ns", i,
                    rx_data.received_symbols[i].level0,
                    rx_data.received_symbols[i].duration0*(1000000000/rmt_tools_cfg.clk_out),
                    rx_data.received_symbols[i].level1,
                    rx_data.received_symbols[i].duration1*(1000000000/rmt_tools_cfg.clk_out));
*/                    
            send_string_to_ws(sendstr);
        }

    }
    rmt_disable(rx_chan);
    cbs.on_recv_done = NULL;
    rmt_rx_register_event_callbacks(rx_chan, &cbs, receive_queue);
    rmt_del_channel(rx_chan);
    vQueueDelete(receive_queue);
    ESP_LOGI(TAG, "Receive OK");
    rmt_receive_started=0;
    vTaskDelete(0);
}
static void rmt_transmit_tools(void *p)
{

    ESP_LOGI(TAG,"Start transmit");
    send_string_to_ws("Start transmit");
    rmt_channel_handle_t tx_chan_handle = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = rmt_tools_cfg.gpio_out,
        .mem_block_symbols = 64,
        .flags.io_loop_back = 1,    // gpio output/input mode
        .resolution_hz = rmt_tools_cfg.clk_out,
        .trans_queue_depth = 5, // set the maximum number of transactions that can pend in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan_handle));

    rmt_copy_encoder_config_t tx_encoder_config = {};
    rmt_encoder_handle_t tx_encoder = NULL;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&tx_encoder_config, &tx_encoder));
    ESP_ERROR_CHECK(rmt_enable(tx_chan_handle));
    trig_set(1);
    rmt_transmit_config_t rmt_tx_config = {0};
    if (rmt_tools_cfg.loop_out)
    {
        rmt_tx_config.loop_count = -1;
    }
    //    for(int i=0;i<20;i++){
    ESP_ERROR_CHECK(rmt_transmit(tx_chan_handle, tx_encoder, rmt_tools_cfg.rmt_data_out, (rmt_tools_cfg.data_out_len) * sizeof(rmt_symbol_word_t), &rmt_tx_config));
    //    }
    if (rmt_tx_wait_all_done(tx_chan_handle, RMT_TIMEOUT_MS) == ESP_ERR_TIMEOUT)
    {
        ESP_LOGI(TAG, "RMT transmit timeout 10 sec");
                send_string_to_ws("RMT transmit timeout 10 sec");
    }

    trig_set(0);
    rmt_disable(tx_chan_handle);
    rmt_del_encoder(tx_encoder);
    rmt_del_channel(tx_chan_handle);

    ESP_LOGI(TAG, "Transmit OK");
    send_string_to_ws("Transmit OK");
    rmt_transmit_started = 0;
    vTaskDelete(0);
}

int cvt_to_clk(char *tok)
{
    if(isdigit((uint8_t)tok[0])==0)
    {
        ESP_LOGE(TAG,"ERR Transmit data format  %s",tok);
        send_string_to_ws("ERR Transmit data format");
    }
    int data = atoi(tok);
    int f = (rmt_tools_cfg.clk_out / 1000000) * rmt_tools_cfg.rmt_ns_mks_ms;
    int ret = (data * f) / 1000; 
    if(ret > 0x7fff) {
        ESP_LOGE(TAG,"ERR Transmit data out of range %d set to max value 32767",ret);
        send_string_to_ws("ERR Transmit data out of range, set to max value 32767");
        ret = 0x7fff;}
    //ESP_LOGI(TAG,"%d",ret);
    return ret;
}
// write ws data from ws to rmt_tools_cfg & run rmt cmd
static void set_rmt_tools_data(char *jsonstr)
{
    char key[32];
    char value[128];
    char *errstr="???";
    esp_err_t err = json_to_str_parm(jsonstr, key, value); // decode json string to key/value pair
    if (err)
    {
        ESP_LOGE(TAG, "ERR jsonstr %s", jsonstr);
        send_string_to_ws("ERR jsonstr");
        return;
    }
    if (strncmp(key, RMT_GPIO_OUT, sizeof(RMT_GPIO_OUT) - 1) == 0)
    {
        rmt_tools_cfg.gpio_out = atoi(value);
    }
    else if (strncmp(key, RMT_CLK_OUT, sizeof(RMT_CLK_OUT) - 1) == 0)
    {
        rmt_tools_cfg.clk_out = atoi(value);
    }
    else if (strncmp(key, RMT_NS_MKS_MS, sizeof(RMT_NS_MKS_MS) - 1) == 0)
    {
        rmt_tools_cfg.rmt_ns_mks_ms = atoi(value);
    }
    else if (strncmp(key, RMT_LOOP_OUT, sizeof(RMT_LOOP_OUT) - 1) == 0)
    {
        rmt_tools_cfg.loop_out = atoi(value);
    }
    else if (strncmp(key, RMT_TRIG_OUT, sizeof(RMT_TRIG_OUT) - 1) == 0)
    {
        rmt_tools_cfg.trig = atoi(value);
        if (rmt_tools_cfg.trig >= 0)
        {
            if(!GPIO_IS_VALID_GPIO(rmt_tools_cfg.trig)){errstr = "Error -> trigger gpio num";goto _err_ret;}
            gpio_reset_pin(rmt_tools_cfg.trig);
            gpio_set_direction(rmt_tools_cfg.trig, GPIO_MODE_INPUT_OUTPUT);
        }
    }
    else if (strncmp(key, RMT_GPIO_IN, sizeof(RMT_GPIO_IN) - 1) == 0)
    {
        rmt_tools_cfg.gpio_in = atoi(value);
    }
    else if (strncmp(key, RMT_CLK_IN, sizeof(RMT_CLK_IN) - 1) == 0)
    {
        rmt_tools_cfg.clk_in = atoi(value);
    }
    else if (strncmp(key, RMT_EOF_MARKER, sizeof(RMT_EOF_MARKER) - 1) == 0)
    {
        rmt_tools_cfg.eof_marker = atoi(value);
    }
    else if (strncmp(key, RMT_IN_OUT_SHORT, sizeof(RMT_IN_OUT_SHORT) - 1) == 0)
    {
        rmt_tools_cfg.in_out_short = atoi(value);
    }

    else if (strncmp(key, RMT_WRITE_DATA_OUT, sizeof(RMT_WRITE_DATA_OUT) - 1) == 0)
    {
        char *tok = strtok(value, " ;");
        int idx = 0;
        while (tok != NULL)
        {

            rmt_tools_cfg.rmt_data_out[idx].duration0 = cvt_to_clk(tok);
            rmt_tools_cfg.rmt_data_out[idx].level0 = 1;
            tok = strtok(NULL, " ;");
            if (tok == NULL)
            {
                break;
            }
            rmt_tools_cfg.rmt_data_out[idx].duration1 = cvt_to_clk(tok);
            rmt_tools_cfg.rmt_data_out[idx].level1 = 0;
            idx++;
            tok = strtok(NULL, " ;");
        }
        rmt_tools_cfg.rmt_data_out[idx].val = 0; // stop transfer
        rmt_tools_cfg.data_out_len = idx + 1;
    }

    // cmd

    else if (strncmp(key, RMT_TRANSMIT_CMD, sizeof(RMT_TRANSMIT_CMD) - 1) == 0)
    {
        if(!GPIO_IS_VALID_GPIO(rmt_tools_cfg.gpio_out)){errstr = "Error -> transmit gpio num out of range ";goto _err_ret;}
        if(rmt_tools_cfg.clk_out<(160000000/255) || rmt_tools_cfg.clk_out>(160000000/2) ){errstr = "Error -> transmit clk out of range ";goto _err_ret;}

        if( !rmt_transmit_started ){
        xTaskCreate(rmt_transmit_tools, "tx_report", 2048 * 2, NULL, 5, NULL);
        rmt_transmit_started = 1;
        } else {errstr = "Error -> transmit already started";goto _err_ret;}
    }
    else if (strncmp(key, RMT_RECEIVE_CMD, sizeof(RMT_RECEIVE_CMD) - 1) == 0)
    {
        if(!GPIO_IS_VALID_GPIO(rmt_tools_cfg.gpio_in)){errstr = "Error -> receive gpio num out of range ";goto _err_ret;}
        if(rmt_tools_cfg.clk_in<(160000000/255) || rmt_tools_cfg.clk_in>(160000000/2) ){errstr = "Error -> receive clk out of range ";goto _err_ret;}
        if(((uint64_t)rmt_tools_cfg.clk_in*rmt_tools_cfg.eof_marker)/1000000000UL > 32767){errstr = "Error -> receive eof out of range ";goto _err_ret;}

        if( !rmt_receive_started ){
        xTaskCreate(rmt_receive_tools, "rx_report", 2048 * 2, NULL, 5, NULL);
        rmt_receive_started = 1;
        } else {errstr = "Error -> receive already started";goto _err_ret;}
    }
    else
    {
        errstr = "Error -> invalid cmd";goto _err_ret;   
    }
    return;
_err_ret:
    ESP_LOGE(TAG, "%s", errstr);
    send_string_to_ws(errstr);
    return;
}
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ra.hd = req->handle;
        ra.fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "Handshake done, the new connection was opened %d", ra.fd);
        // read & send initial rmt data from rmt_tools_cfg
        send_default_rmt_tools_cfg_to_ws();
        if (rmt_tools_cfg.trig >= 0)
        {
            gpio_reset_pin(rmt_tools_cfg.trig);
            gpio_set_direction(rmt_tools_cfg.trig, GPIO_MODE_OUTPUT);
        }
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    if (ws_pkt.len)
    {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
    }
    set_rmt_tools_data((char *)ws_pkt.payload);
    free(buf);
    return ret;
}
static esp_err_t get_handler(httpd_req_t *req)
{
    extern const unsigned char rmt_tools_html_html_start[] asm("_binary_rmt_tools_html_html_start");
    extern const unsigned char rmt_tools_html_html_end[] asm("_binary_rmt_tools_html_html_end");
    const size_t rmt_tools_html_html_size = (rmt_tools_html_html_end - rmt_tools_html_html_start);

    httpd_resp_send_chunk(req, (const char *)rmt_tools_html_html_start, rmt_tools_html_html_size);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}
static const httpd_uri_t rmt_tools_gh = {
    .uri = CONFIG_RMT_TOOLS_WEB_URI,
    .method = HTTP_GET,
    .handler = get_handler,
    .user_ctx = NULL};
static const httpd_uri_t rmt_tools_ws = {
    .uri = CONFIG_RMT_TOOLS_WEB_WS_URI,
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true};
// register uri handler to ws server
esp_err_t rmt_tools_register_uri_handlers(httpd_handle_t server)
{
    esp_err_t ret = ESP_OK;
    ret = httpd_register_uri_handler(server, &rmt_tools_gh);
    if (ret)
        goto _ret;
    ret = httpd_register_uri_handler(server, &rmt_tools_ws);
    if (ret)
        goto _ret;
_ret:
    return ret;
}
