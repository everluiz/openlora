set(COMPONENT_SRCS "hello_world_main.c" "../components/lora/lora.c" "u8g2_esp32_hal.c" "u8g2_esp32_hal.h " "../components/filetransfer/filetransfer.c")
set(COMPONENT_ADD_INCLUDEDIRS "../components/lora/include")
set(COMPONENT_ADD_INCLUDEDIRS "../components/backoff/include")
set(COMPONENT_ADD_INCLUDEDIRS "../components/filetransfer/include")

register_component()



