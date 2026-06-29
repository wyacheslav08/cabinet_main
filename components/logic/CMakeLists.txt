idf_component_register(SRCS "climate_control.c" "weight_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES cabinet_hal storage sensors)