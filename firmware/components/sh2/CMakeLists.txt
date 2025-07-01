idf_component_register(
    SRCS
        "src/sh2.c"
        "src/sh2_SensorValue.c"
        "src/sh2_util.c"
        "src/shtp.c"
    INCLUDE_DIRS "src"
    REQUIRES driver
) 