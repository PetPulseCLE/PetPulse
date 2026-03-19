set(ACCONEER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

idf_component_register(SRCS "${ACCONEER_ROOT}/source/example_detector_distance.c"
                       SRCS "${ACCONEER_ROOT}/source/acc_algorithm.c"
                       SRCS "${ACCONEER_ROOT}/source/acc_control_helper.c"
                       SRCS "${ACCONEER_ROOT}/source/acc_hal_integration_espidf_xe121.c"
                       SRCS "${ACCONEER_ROOT}/source/acc_integration_esp32.c"
                       SRCS "${ACCONEER_ROOT}/source/acc_integration_log.c"
                       SRCS "${ACCONEER_ROOT}/source/acc_processing_helpers.c"
                       INCLUDE_DIRS "${ACCONEER_ROOT}/include")

add_prebuilt_library(libacc_detector_distance_a121 "${ACCONEER_ROOT}/lib/libacc_detector_distance_a121.a")
add_prebuilt_library(libacc_detector_presence_a121 "${ACCONEER_ROOT}/lib/libacc_detector_presence_a121.a")
add_prebuilt_library(libacconeer_a121 "${ACCONEER_ROOT}/lib/libacconeer_a121.a")

target_link_libraries(${COMPONENT_TARGET} INTERFACE libacc_detector_presence_a121 libacc_detector_distance_a121 libacconeer_a121)

include(CheckCCompilerFlag)
check_c_compiler_flag("-ffile-prefix-map=foo=bar" SUPPORTS_FFILE_PREFIX_MAP)

if(SUPPORTS_FFILE_PREFIX_MAP)
    # Strip the full path from E.g. __FILE__ to reduce binary size
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffile-prefix-map=${CMAKE_CURRENT_LIST_DIR}=.")
endif()
