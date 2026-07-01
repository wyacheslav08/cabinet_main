idf_component_register(SRCS "display_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES cabinet_hal u8g2 logic sensors comm)