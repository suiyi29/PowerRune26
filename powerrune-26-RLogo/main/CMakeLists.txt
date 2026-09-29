idf_component_register(SRCS "LED_Strip.cpp" "LED.cpp" "firmware.cpp" "espnow_protocol.cpp" "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES
                        esp_wifi
                        nvs_flash
                        esp_http_client
                        esp_https_ota
                        app_update
                        driver
                        esp_event
                        esp_netif
                        bt)