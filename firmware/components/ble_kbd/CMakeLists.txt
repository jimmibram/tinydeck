idf_component_register(SRCS "ble_kbd.c" "esp_hid_gap.c"
                       INCLUDE_DIRS "include" "."
                       REQUIRES bt esp_hid
                       PRIV_REQUIRES nvs_flash esp_timer)
