#ifndef PICTURE_LIB_JPG_CODEC_H
#define PICTURE_LIB_JPG_CODEC_H

#include <stdio.h>
#include <stdint.h>

typedef enum {
    JPG_CODEC_RET_OK = 0,
    JPG_CODEC_BAD_ARG = -1,
    JPG_CODEC_NOMEM = 2,
    JPG_CODEC_FILE_CORRUPTED = -3,
    JPG_CODEC_NO_JPG_FILE = -4,
    JPG_CODEC_UNSUPPORTED_MARKER = -5,
    JPG_CODEC_NULLPTR_ERR = -6,
    

} jpg_codec_ret_code_t;

jpg_codec_ret_code_t jpg_codec_file_decode(FILE *jpg_file, void **out_pixel_array);

jpg_codec_ret_code_t jpg_codec_memory_decode(const uint8_t *jpg_data, size_t len, void **out_pixel_array);

#endif /* PICTURE_LIB_JPG_CODEC_H */
