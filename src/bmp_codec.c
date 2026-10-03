#include "bmp_codec.h"
#include <stdio.h>
#include <stdlib.h>

#pragma pack(push, 1)
typedef struct {
    int16_t     bfType;
    int32_t    bfSize;
    int16_t     reserved[2];
    int32_t    bfOffBits;
    int32_t    biSize;
    int32_t    biWidth;
    int32_t    biHeight;
    int16_t     biPlanes;
    int16_t     biBitCount;
    int32_t    biCompression;
    int32_t    biSizeImage;
    int32_t    biXPelsPerMeter;
    int32_t    biYPelsPerMeter;
    int32_t    biClrUsed;
    int32_t    biClrImportant;
} bmp_file_header;
#pragma pack(pop)

int  bmp_file_create(rgb_pixel_t **pixel_matrix, int height, int width)
{
    bmp_file_header file_header = {
        .bfType = 0x4D42,
        .bfSize = height*width*3 + sizeof(bmp_file_header),
        .bfOffBits = sizeof(bmp_file_header),
        .biSize = 40,
        .biWidth = width,
        .biHeight = height,
        .biPlanes = 1,
        .biBitCount = 24,
        .biSizeImage = height*width*3
    };


    FILE *output = NULL;
    int number;
 
    output = fopen("../test_pic/test.bmp", "wb");
    if (!output)
        return -2;

    fwrite(&file_header, sizeof(file_header), 1, output);

    for (int i = height-1; i >= 0; i--){
        for (int j = 0; j < width; j++){
            fwrite(&pixel_matrix[i][j].B, sizeof(uint8_t), 1, output);
            fwrite(&pixel_matrix[i][j].G, sizeof(uint8_t), 1, output);
            fwrite(&pixel_matrix[i][j].R, sizeof(uint8_t), 1, output);
        }
    }

    fclose(output);

    return 0;
}