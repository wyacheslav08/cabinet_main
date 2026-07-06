idf_component_register(
    SRCS "display_manager.c"
         "font_cyrillic_8.c"
         "font_cyrillic_12.c"
         "font_cyrillic_16.c"
         "font_cyrillic_20.c"
         "font_cyrillic_24.c"
         "font_cyrillic_48.c"
         
    INCLUDE_DIRS "include" "."
    
    # Публичные зависимости (доступны тем, кто подключает ui)
    REQUIRES 
        lvgl
        esp_timer
        driver
        
    # Приватные зависимости (скрыты внутри display_manager.c)
    PRIV_REQUIRES 
        esp_lcd
        esp_lvgl_port
        esp_lcd_st7735-main
        cabinet_hal
        log
)