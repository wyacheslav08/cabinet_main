idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES cabinet_hal comm storage nvs_flash sensors logic dsp_audio ui)