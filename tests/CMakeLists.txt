add_executable(picture_lib_tests
    test_main.c
)

target_link_libraries(picture_lib_tests PRIVATE picture_lib)

add_test(
    NAME picture_lib_tests
    COMMAND picture_lib_tests
)
