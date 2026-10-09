idf_component_register(SRCS "main.c" "input.c" "shell.c" "usb_console.c"
                       INCLUDE_DIRS "."
                       REQUIRES term ble_kbd rlcd
                       PRIV_REQUIRES nvs_flash esp_driver_usb_serial_jtag esp_timer vfs esp_app_format)
