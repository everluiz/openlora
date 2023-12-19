set(COMPONENT_SRCS "filetransfer.c")
set(COMPONENT_ADD_INCLUDEDIRS "include" "../lora/include")
set(COMPONENT_REQUIRES spiffs vfs spi_flash nvs_flash fatfs)
register_component()
