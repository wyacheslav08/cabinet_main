idf_component_register(
    # Указываем все C-файлы нашего компонента логики
    SRCS "climate_control.c" 
         "weight_manager.c"
         
    # Папка с заголовочными файлами компонента logic
    INCLUDE_DIRS "include"
    
    # Указываем зависимости: к каким другим компонентам мы обращаемся
    REQUIRES 
        cabinet_hal     # Чтобы видеть pwm_manager.h
        sensors         # Чтобы видеть sht40_driver.h
        ui              # <--- ИМЕННО ЭТО РЕШАЕТ ОШИБКУ (видим display_manager.h)
        freertos        # Для vTaskDelay и задач
        log             # Для ESP_LOGI
)