add_executable(jpg_bitflow_tests
    test_bitflow.c
)

target_link_libraries(jpg_bitflow_tests PRIVATE picture_lib)

add_test(
    NAME jpg_bitflow_tests
    COMMAND jpg_bitflow_tests
)

add_executable(jpg_byteflow_tests
    test_byteflow.c
)

target_link_libraries(jpg_byteflow_tests PRIVATE picture_lib)

add_test(
    NAME jpg_byteflow_tests
    COMMAND jpg_byteflow_tests
)

add_executable(jpg_codec_tests
    test_jpg_codec.c
)

target_link_libraries(jpg_codec_tests PRIVATE picture_lib)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test_pic")

add_test(
    NAME jpg_codec_tests
    COMMAND jpg_codec_tests
)

set_tests_properties(jpg_codec_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
