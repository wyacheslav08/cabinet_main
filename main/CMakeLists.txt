idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        ui 
        cabinet_hal 
        sensors       # <--- КРИТИЧНО ВАЖНО: main должен видеть сенсоры
        logic         
        nvs_flash 
        freertos 
        esp_system 
        log
)