| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |

# ESP-IDF I2C Tools with websocket interface

## Overview

- Simple clone [ESP-IDF i2c_Tools](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/i2c/i2c_tools) with websocket interface
- connects as a standard ESP-IDF component
- can be used to test i2c devices and as an i2c signal generator for [logic_analyzer](https://github.com/ok-home/logic_analyzer)

## How to use 

  - Connect as standart ESP-IDF component
  - Start a Wi-Fi connection rmt_tools_wifi_connect() or use an existing one
  - Run a web server rmt_tools_ws_server or use an existing web server with websocket support
  - Register rmt_tools handlers ( web page & ws handlers) rmt_tools_register_uri_handlers(httpd_handle_t server)
  - Go to the device web page

Use as example [rmt_ws_tools-example](https://github.com/ok-home/rmt_ws_tools/tree/main/rmt_ws_tools_example)

### Configure the project

Open the project configuration menu (`idf.py menuconfig`). Then go into `I2C WS Tools Configuration` menu.

### Build and Flash

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

