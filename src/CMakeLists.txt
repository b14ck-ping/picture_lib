cmake_minimum_required(VERSION 3.10) 
set(CMAKE_C_STANDARD 11)
add_compile_options(-lm)

project(picture_lib)

set(SOURCE jpg_codec.c huffman.c bmp_codec.c jpg_bitflow.c jpg_byteflow.c log.c)#
set(HEADER jpg_common.h jpg_codec.h huffman.h bmp_codec.h DCT_coefficients.h jpg_bitflow.h jpg_byteflow.h log.h)#

add_library(picture_lib STATIC ${HEADER} ${SOURCE})

target_link_libraries(picture_lib m)
