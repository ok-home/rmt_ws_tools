#pragma once
/*
*   simple 
*   with websocket interface
*   connects as a standard ESP-IDF component
*   can be used to test rmt devices and as an rmt signal generator for logic_analyzer https://github.com/ok-home/logic_analyzer
*/
#include <esp_log.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_RMT_TOOLS_START_WS_SERVER
/*
*   @brief  start  httpd server with websocket support
*           connect on existing WIFI STA connections or rmt_tools_wifi_connect()
*           enable on menuconfig RMT_TOOLS_START_WS_SERVER
*   @return  
*           httpd_handle_t server -> server handle on started web server
*           NULL                  -> server start FAIL
*/
httpd_handle_t rmt_tools_ws_server(void);

#ifdef CONFIG_RMT_TOOLS_WIFI_CONNECT
/*
*   @brief  connect to wifi
*           connect mode -> sta 
*           ssid/pass -> read from menuconfig -> RMT_TOOLS_WIFI_SSID/RMT_TOOLS_WIFI_PASS
*           enable on menuconfig RMT_TOOLS_START_WS_SERVER & RMT_TOOLS_WIFI_CONNECT
*   @return
*           ESP_OK      -> connect to wifi OK with RMT_TOOLS_WIFI_SSID/RMT_TOOLS_WIFI_PASS
*           ESP_FAIL    -> can`t connect to wifi
*/
esp_err_t rmt_tools_wifi_connect();
#endif //CONFIG_RMT_TOOLS_WIFI_CONNECT
#endif //CONFIG_RMT_TOOLS_START_WS_SERVER


/*
*   @brief  register rmt_tools handlers ( web page & ws handlers) on existing  httpd server with ws support
*           uri page -> menuconfig -> CONFIG_RMT_TOOLS_WEB_URI/CONFIG_RMT_TOOLS_WEB_WS_URI
*   @param  httpd_handle_t server -> existing server handle
*   @return
*           ESP_OK      -> register OK
*           ESP_FAIL    -> register FAIL
*/
esp_err_t rmt_tools_register_uri_handlers(httpd_handle_t server);

#ifdef __cplusplus
}
#endif