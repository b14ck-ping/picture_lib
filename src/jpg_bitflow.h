#ifndef PICTURE_LIB_JPG_BITFLOW_H
#define PICTURE_LIB_JPG_BITFLOW_H

#include <stdio.h>
#include <stdint.h>

struct jpg_bitflow_t;

typedef enum {
    JPG_BITFLOW_RET_OK = 0,
    JPG_BITFLOW_BAD_ARG = -1,
    JPG_BITFLOW_NOMEM = 2,
    JPG_BITFLOW_END_OF_FLOW = -3,
    JPG_BITFLOW_RANGE = -4,
    JPG_BITFLOW_FOUND_EOI_MARKER = -5,
    JPG_BITFLOW_FOUND_RST_MARKER = -6,
} jpg_bitflow_ret_code_t;

struct jpg_bitflow_t* jpg_bitflow_allocate(const uint8_t* flow, size_t le);

jpg_bitflow_ret_code_t jpg_bitflow_deallocate(struct jpg_bitflow_t*);

jpg_bitflow_ret_code_t jpg_bitflow_get_next_n_bits(struct jpg_bitflow_t* bf, uint8_t* pbuff, uint32_t n);

jpg_bitflow_ret_code_t jpg_bitflow_get_next_bit(struct jpg_bitflow_t* bf, int* pbit);

jpg_bitflow_ret_code_t jpg_bitflow_get_next_i64_bits(struct jpg_bitflow_t* bf, int64_t* pbuff, uint32_t n);

#endif /* PICTURE_LIB_JPG_BITFLOW_H */
