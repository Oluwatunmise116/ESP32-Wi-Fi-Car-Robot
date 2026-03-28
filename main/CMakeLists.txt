idf_component_register(
    SRCS        "main.c"
    INCLUDE_DIRS "."
    REQUIRES    wifi_manager esp_http_server driver nvs_flash log)
