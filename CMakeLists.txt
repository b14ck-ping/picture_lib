cmake_minimum_required(VERSION 3.10) 

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-lm)

project(test_jpg_codec)

include(CTest)

include_directories(src)
add_subdirectory(src)	

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
			
add_executable(main main.c)	

target_link_libraries(main m)
target_link_libraries(main picture_lib)	
