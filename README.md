| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |

# ESP-IDF RMT Tools with websocket interface

## Overview

- connects as a standard ESP-IDF component
- can be used to test RMT devices and as an RMT signal generator for [logic_analyzer](https://github.com/ok-home/logic_analyzer)

## How to use 

  - Connect as standart ESP-IDF component
  - Start a Wi-Fi connection rmt_tools_wifi_connect() or use an existing one
  - Run a web server rmt_tools_ws_server or use an existing web server with websocket support
  - Register rmt_tools handlers ( web page & ws handlers) rmt_tools_register_uri_handlers(httpd_handle_t server)
  - Go to the device web page

Use as example [rmt_ws_tools_example](https://github.com/ok-home/rmt_ws_tools/tree/main/rmt_ws_tools_example)

### Configure the project

Open the project configuration menu (`idf.py menuconfig`). Then go into `RMT WS Tools Configuration` menu.

### Build and Flash

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

