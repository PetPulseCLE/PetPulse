idf_component_register(
    SRCS "source/ws2812b.c"
         "source/sht4x.c"
    INCLUDE_DIRS "include"
    REQUIRES driver BSP
)
