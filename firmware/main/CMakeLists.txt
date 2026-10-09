idf_component_register(SRCS "main.c" "usb_console.c"
                       INCLUDE_DIRS "."
                       REQUIRES ui term ble_kbd rlcd input sensors
                       PRIV_REQUIRES nvs_flash esp_driver_usb_serial_jtag esp_timer vfs)
