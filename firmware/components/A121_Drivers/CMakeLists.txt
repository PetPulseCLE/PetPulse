idf_component_register(
    SRCS "source/a121_radar_driver.c"
         "source/radar_config.c"
         "source/acc_algorithm.c"
         "source/acc_hal_integration_espidf_xe121.c"
         "source/acc_integration_esp32.c"
         "source/acc_integration_log.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_spi BSP
)

add_prebuilt_library(libacc_detector_distance_a121 "${CMAKE_CURRENT_SOURCE_DIR}/lib/libacc_detector_distance_a121.a")
add_prebuilt_library(libacc_detector_presence_a121  "${CMAKE_CURRENT_SOURCE_DIR}/lib/libacc_detector_presence_a121.a")
add_prebuilt_library(libacconeer_a121               "${CMAKE_CURRENT_SOURCE_DIR}/lib/libacconeer_a121.a")

target_link_libraries(${COMPONENT_TARGET} INTERFACE
    libacc_detector_presence_a121
    libacc_detector_distance_a121
    libacconeer_a121
)

include(CheckCCompilerFlag)
check_c_compiler_flag("-ffile-prefix-map=foo=bar" SUPPORTS_FFILE_PREFIX_MAP)
if(SUPPORTS_FFILE_PREFIX_MAP)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.
    )
endif()
