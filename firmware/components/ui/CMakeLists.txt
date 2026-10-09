idf_component_register(SRCS "ui.c" "topbar.c" "app_home.c" "app_terminal.c" "shell.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx term input sensors ble_kbd rlcd
                       PRIV_REQUIRES esp_timer esp_app_format)
