idf_component_register(
    SRCS "main.c"
    
    INCLUDE_DIRS "."
    
    # Подключаем только высокоуровневые модули нашего проекта и системные библиотеки.
    # Напрямую LVGL или драйверы дисплея сюда подключать НЕ НУЖНО (инкапсуляция).
    REQUIRES 
        ui 
        cabinet_hal 
        nvs_flash 
        freertos 
        esp_system 
        log
)