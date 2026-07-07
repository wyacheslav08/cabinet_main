idf_component_register(
    SRCS "display_manager.c"
         "font_cyrillic_8.c"
         "font_cyrillic_12.c"
         "font_cyrillic_16.c"
         "font_cyrillic_20.c"
         "font_cyrillic_24.c"
         "font_cyrillic_48.c"
         
    INCLUDE_DIRS "include" "."
    
    # ПУБЛИЧНЫЕ ЗАВИСИМОСТИ (REQUIRES)
    # Эти компоненты будут видны не только внутри display_manager.c,
    # но и всем, кто подключит себе display_manager.h (например, main.c)
    REQUIRES 
        lvgl
        esp_timer
        driver
        sensors       # <--- ДОБАВЛЕНО: Теперь ui видит gesture_manager.h
        
    # ПРИВАТНЫЕ ЗАВИСИМОСТИ (PRIV_REQUIRES)
    # Эти компоненты нужны ТОЛЬКО для компиляции display_manager.c
    PRIV_REQUIRES 
        esp_lcd
        esp_lvgl_port
        esp_lcd_st7735-main
        cabinet_hal
        log
        storage       # Если внутри .c вы вызываете settings_manager.h
)