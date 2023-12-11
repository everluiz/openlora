set(COMPONENT_SRCS "app.c" "ftp.c")
set(COMPONENT_ADD_INCLUDEDIRS "include" "../lora/include")
set(COMPONENT_REQUIRES spi_flash nvs_flash esp_wifi fatfs)

register_component()
