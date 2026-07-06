idf_component_register(
    SRCS "i2c_manager.c" 
         "pwm_manager.c" 
         "hx711_driver.c"
         
    INCLUDE_DIRS "include"
    
    # Добавили "comm", чтобы pwm_manager.c видел uart_link.h
    REQUIRES 
        driver 
        esp_timer 
        freertos 
        comm
)