idf_component_register(
    SRCS "sht40_driver.c" 
         "mpr121_driver.c" 
         "gesture_manager.c"   # <--- КРИТИЧНО ВАЖНО: Без этого кода не будет в памяти
         
    INCLUDE_DIRS "include"
    
    REQUIRES 
        driver 
        cabinet_hal 
        storage
)