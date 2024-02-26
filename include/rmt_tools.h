#pragma once
/*
*   simple clone
*   ESP-IDF i2c_Tools https://github.com/espressif/esp-idf/tree/master/examples/peripherals/i2c/i2c_tools
*   with websocket interface
*   connects as a standard ESP-IDF component
*   can be used to test i2c devices and as an i2c signal generator for logic_analyzer https://github.com/ok-home/logic_analyzer
*/
#include <esp_log.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_I2C_TOOLS_START_WS_SERVER
/*
*   @brief  start  httpd server with websocket support
*           connect on existing WIFI STA connections or rmt_tools_wifi_connect()
*           enable on menuconfig I2C_TOOLS_START_WS_SERVER
*   @return  
*           httpd_handle_t server -> server handle on started web server
*           NULL                  -> server start FAIL
*/
httpd_handle_t rmt_tools_ws_server(void);

#ifdef CONFIG_I2C_TOOLS_WIFI_CONNECT
/*
*   @brief  connect to wifi
*           connect mode -> sta 
*           ssid/pass -> read from menuconfig -> I2C_TOOLS_WIFI_SSID/I2C_TOOLS_WIFI_PASS
*           enable on menuconfig I2C_TOOLS_START_WS_SERVER & I2C_TOOLS_WIFI_CONNECT
*   @return
*           ESP_OK      -> connect to wifi OK with I2C_TOOLS_WIFI_SSID/I2C_TOOLS_WIFI_PASS
*           ESP_FAIL    -> can`t connect to wifi
*/
esp_err_t rmt_tools_wifi_connect();
#endif //CONFIG_I2C_TOOLS_WIFI_CONNECT
#endif //CONFIG_I2C_TOOLS_START_WS_SERVER


/*
*   @brief  register rmt_tools handlers ( web page & ws handlers) on existing  httpd server with ws support
*           uri page -> menuconfig -> CONFIG_I2C_TOOLS_WEB_URI/CONFIG_I2C_TOOLS_WEB_WS_URI
*   @param  httpd_handle_t server -> existing server handle
*   @return
*           ESP_OK      -> register OK
*           ESP_FAIL    -> register FAIL
*/
esp_err_t rmt_tools_register_uri_handlers(httpd_handle_t server);

#ifdef __cplusplus
}
#endif