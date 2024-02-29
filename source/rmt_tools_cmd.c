/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
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

// web cfg transmit
#define RMT_CHANNEL_OUT "RMTChannelOut"
#define RMT_GPIO_OUT "RMTGPIOOut"
#define RMT_CLK_OUT "RMTClkOut"
#define RMT_LOOP_OUT "RMTLoopOut"
#define RMT_TRIG_OUT "RMTTrigGPIO"
#define RMT_WRITE_DATA_OUT "RMTWriteData"
// cmd
#define RMT_TRANSMIT_CMD "RMTStartOut"
// web cfg receive
#define RMT_CHANNEL_IN "RMTChannelIn"
#define RMT_GPIO_IN "RMTGPIOIn"
#define RMT_CLK_IN "RMTClkIn"
#define RMT_IN_OUT_SHORT "RMTInOutShort"
// cmd
#define RMT_RECEIVE_CMD "RMTStartIn"

typedef struct rmt_tools_cfg
{
    int channel_out;
    int gpio_out;
    int clk_out;
    int loop_out;
    int trig;
    rmt_symbol_word_t rmt_data_out[64];
    int data_out_len;

    int channel_in;
    int gpio_in;
    int clk_in;
    int in_out_shot;
    rmt_symbol_word_t rmt_data_in[64];
    int data_in_len;

} rmt_tools_cfg_t;

static rmt_tools_cfg_t rmt_tools_cfg = {0};

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
static void send_string_to_ws(char *str, httpd_req_t *req)
{
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = (uint8_t *)str;
    ws_pkt.len = strlen(str);
    httpd_ws_send_frame(req, &ws_pkt);
}

static void send_default_rmt_tools_cfg_to_ws(httpd_req_t *req)
{
    char jsonstr[64] = {0};
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_CHANNEL_OUT, rmt_tools_cfg.channel_out);
    send_string_to_ws(jsonstr, req);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_GPIO_OUT, rmt_tools_cfg.gpio_out);
    send_string_to_ws(jsonstr, req);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_CLK_OUT, rmt_tools_cfg.clk_out);
    send_string_to_ws(jsonstr, req);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_LOOP_OUT, rmt_tools_cfg.loop_out);
    send_string_to_ws(jsonstr, req);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_TRIG_OUT, rmt_tools_cfg.trig);
    send_string_to_ws(jsonstr, req);

    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_CHANNEL_IN, rmt_tools_cfg.channel_in);
    send_string_to_ws(jsonstr, req);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_GPIO_IN, rmt_tools_cfg.gpio_in);
    send_string_to_ws(jsonstr, req);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_CLK_IN, rmt_tools_cfg.clk_in);
    send_string_to_ws(jsonstr, req);
    sprintf(jsonstr, "{\"name\":\"%s\",\"msg\":\"%d\"}", RMT_IN_OUT_SHORT, rmt_tools_cfg.in_out_shot);
    send_string_to_ws(jsonstr, req);
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
rmt_rx_event_callbacks_t cbs = {
    .on_recv_done = rmt_rx_done_callback,
};

static void rmt_receive_tools(void *p)
{
    httpd_req_t *req = (httpd_req_t *)p;

    ESP_LOGI(TAG, "RECEIVE");
    send_string_to_ws("RECEIVE  ", req);

    rmt_channel_handle_t rx_chan = NULL;
    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // select source clock
        .resolution_hz = rmt_tools_cfg.clk_in, // 1 MHz tick resolution, i.e., 1 tick = 1 µs
        .mem_block_symbols = 64,               // memory block size, 64 * 4 = 256 Bytes
        .gpio_num = rmt_tools_cfg.gpio_in,     // GPIO number
        .flags.invert_in = false,              // do not invert input signal
        .flags.with_dma = false,               // do not need DMA backend
        .flags.io_loop_back = 1,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rx_chan));
    receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, receive_queue));
    ESP_ERROR_CHECK(rmt_enable(rx_chan));
    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 100,        // the shortest duration for NEC signal is 560 µs, 1250 ns < 560 µs, valid signal is not treated as noise
        .signal_range_max_ns = 100 * 1000, // the longest duration for NEC signal is 9000 µs, 12000000 ns > 9000 µs, the receive does not stop early
    };
    ESP_ERROR_CHECK(rmt_receive(rx_chan, rmt_tools_cfg.rmt_data_in, sizeof(rmt_tools_cfg.rmt_data_in), &receive_config));
    rmt_rx_done_event_data_t rx_data;
    xQueueReceive(receive_queue, &rx_data, portMAX_DELAY);
    for (int i = 0; i < rx_data.num_symbols; i++)
    {
        ESP_LOGI("received ", "%d 0->%d 1->%d", i, rx_data.received_symbols[i].duration0, rx_data.received_symbols[i].duration1);
    }
    vQueueDelete(receive_queue);
    rmt_disable(tx_chan_handle);
    rmt_del_channel(tx_chan_handle);
    vTaskDelete(0);
}
static void rmt_transmit_tools(httpd_req_t *req)
{
    ESP_LOGI(TAG, "TRANSMIT %d %d %d ", rmt_tools_cfg.gpio_out, rmt_tools_cfg.clk_out, rmt_tools_cfg.data_out_len);
    send_string_to_ws("TRANSMIT  ", req);
    for (int i = 0; i < rmt_tools_cfg.data_out_len; i++)
        ESP_LOGI("DATA", " %d %ld", sizeof(rmt_symbol_word_t), rmt_tools_cfg.rmt_data_out[i].val);
    rmt_channel_handle_t tx_chan_handle = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = rmt_tools_cfg.gpio_out,
        .mem_block_symbols = 64,
        .flags.io_loop_back = 1,
        .resolution_hz = rmt_tools_cfg.clk_out,
        .trans_queue_depth = 10, // set the maximum number of transactions that can pend in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan_handle));

    rmt_copy_encoder_config_t tx_encoder_config = {};
    rmt_encoder_handle_t tx_encoder = NULL;
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&tx_encoder_config, &tx_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(tx_chan_handle));
    trig_set(1);
    rmt_transmit_config_t rmt_tx_config = {0};
    ESP_ERROR_CHECK(rmt_transmit(tx_chan_handle, tx_encoder, rmt_tools_cfg.rmt_data_out, (rmt_tools_cfg.data_out_len) * sizeof(rmt_symbol_word_t), &rmt_tx_config));

    if (rmt_tx_wait_all_done(tx_chan_handle, 5000) == ESP_ERR_TIMEOUT)
    {
        ESP_LOGI(TAG, "RMT TIMEOUT 5sec");
    }
    trig_set(0);
    rmt_del_encoder(tx_encoder);
    rmt_disable(tx_chan_handle);
    rmt_del_channel(tx_chan_handle);

    ESP_LOGI(TAG, "TRANSMIT DONE");
    send_string_to_ws("TRANSMIT DONE  ", req);
}

// write ws data from ws to rmt_tools_cfg & run rmt cmd
static void set_rmt_tools_data(char *jsonstr, httpd_req_t *req)
{
    char key[32];
    char value[128];
    esp_err_t err = json_to_str_parm(jsonstr, key, value); // decode json string to key/value pair
    if (err)
    {
        ESP_LOGE(TAG, "ERR jsonstr %s", jsonstr);
        send_string_to_ws("ERR jsonstr", req);
        send_string_to_ws(jsonstr, req);
        return;
    }
    if (strncmp(key, RMT_CHANNEL_OUT, sizeof(RMT_CHANNEL_OUT) - 1) == 0)
    {
        rmt_tools_cfg.channel_out = atoi(value);
    }
    else if (strncmp(key, RMT_GPIO_OUT, sizeof(RMT_GPIO_OUT) - 1) == 0)
    {
        rmt_tools_cfg.gpio_out = atoi(value);
    }
    else if (strncmp(key, RMT_CLK_OUT, sizeof(RMT_CLK_OUT) - 1) == 0)
    {
        rmt_tools_cfg.clk_out = atoi(value);
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
            gpio_reset_pin(rmt_tools_cfg.trig);
            gpio_set_direction(rmt_tools_cfg.trig, GPIO_MODE_OUTPUT);
        }
    }
    else if (strncmp(key, RMT_CHANNEL_IN, sizeof(RMT_CHANNEL_IN) - 1) == 0)
    {
        rmt_tools_cfg.channel_in = atoi(value);
    }
    else if (strncmp(key, RMT_GPIO_IN, sizeof(RMT_GPIO_IN) - 1) == 0)
    {
        rmt_tools_cfg.gpio_in = atoi(value);
    }

    else if (strncmp(key, RMT_CLK_IN, sizeof(RMT_CLK_IN) - 1) == 0)
    {
        rmt_tools_cfg.clk_in = atoi(value);
    }
    else if (strncmp(key, RMT_IN_OUT_SHORT, sizeof(RMT_IN_OUT_SHORT) - 1) == 0)
    {
        rmt_tools_cfg.in_out_shot = atoi(value);
    }

    else if (strncmp(key, RMT_WRITE_DATA_OUT, sizeof(RMT_WRITE_DATA_OUT) - 1) == 0)
    {
        char *tok = strtok(value, " ;");
        int idx = 0;
        while (tok != NULL)
        {
            ESP_LOGI(TAG, "0- %s", tok);
            rmt_tools_cfg.rmt_data_out[idx].duration0 = atoi(tok);
            rmt_tools_cfg.rmt_data_out[idx].level0 = 1;
            tok = strtok(NULL, " ;");
            ESP_LOGI(TAG, "1- %s", tok);
            rmt_tools_cfg.rmt_data_out[idx].duration1 = atoi(tok);
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
        rmt_transmit_tools(req);
    }
    else if (strncmp(key, RMT_RECEIVE_CMD, sizeof(RMT_RECEIVE_CMD) - 1) == 0)
    {
        xTaskCreate(rmt_receive_tools,"rx_report",2048*2,(void*)&req,5,NULL);
        //rmt_receive_tools(req);
    }

    else
    {
        ESP_LOGE(TAG, "ERR cmd %s", jsonstr);
        send_string_to_ws("ERR cmd", req);
        send_string_to_ws(jsonstr, req);
    }

    return;
}
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened %d", httpd_req_to_sockfd(req));
        // read & send initial rmt data from rmt_tools_cfg
        send_default_rmt_tools_cfg_to_ws(req);
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
    // ESP_LOGI(TAG,"get cmd %s",(char *)ws_pkt.payload);
    //  set ws data to rmt_tools_cfg & get rmt cmd
    set_rmt_tools_data((char *)ws_pkt.payload, req);
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
