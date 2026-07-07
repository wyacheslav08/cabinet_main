idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        ui 
        cabinet_hal 
        sensors       # Добавлено для gesture_manager.h
        logic         # Добавлено для climate_control.h
        nvs_flash 
        freertos 
        esp_system 
        log
)