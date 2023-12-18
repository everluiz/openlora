set(COMPONENT_SRCS "hello_world_main.c" "../components/lora/lora.c" "u8g2_esp32_hal.c" "u8g2_esp32_hal.h " "../components/esp-idf-ftpServer/app.c" "../components/esp-idf-ftpClient/Client_app.c")
set(COMPONENT_ADD_INCLUDEDIRS "../components/lora/include")
set(COMPONENT_ADD_INCLUDEDIRS "../components/backoff/include")
set(COMPONENT_ADD_INCLUDEDIRS "../components/esp-idf-ftpServer/include")
set(COMPONENT_ADD_INCLUDEDIRS "../components/esp-idf-ftpClient/include")

register_component()



