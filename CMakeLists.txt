set( srcs
    source/i2c_tools_cmd.c
)
if(CONFIG_I2C_TOOLS_START_WS_SERVER)
    list( APPEND srcs
        source/i2c_tools_web_server.c
    )
    if(CONFIG_I2C_TOOLS_WIFI_CONNECT)
        list( APPEND srcs
            source/i2c_tools_wifi_connect.c
    )
    endif()
endif()

idf_component_register(
    SRCS 
        ${srcs}
    INCLUDE_DIRS 
        include
    PRIV_INCLUDE_DIRS 
        private_include
    REQUIRES 
       esp_http_server
    PRIV_REQUIRES 
        nvs_flash 
        esp_wifi
        driver
    EMBED_FILES 
        source/i2c_tools_html.html
)
