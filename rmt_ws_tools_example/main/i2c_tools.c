#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include "i2c_tools.h"
#include "logic_analyzer_ws.h"

void app_main(void)
{
    i2c_tools_wifi_connect();
    httpd_handle_t server = i2c_tools_ws_server();
    i2c_tools_register_uri_handlers(server);
    logic_analyzer_register_uri_handlers(server);
}

