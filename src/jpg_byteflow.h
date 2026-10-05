#ifndef PICTURE_LIB_JPG_BYTEFLOW_H
#define PICTURE_LIB_JPG_BYTEFLOW_H

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

typedef enum {
    JPG_BYTEFLOW_RET_OK = 0,
    JPG_BYTEFLOW_BAD_ARG = -1,
    JPG_BYTEFLOW_NOMEM = 2,
    JPG_BYTEFLOW_END_OF_FLOW = -3,
    JPG_BYTEFLOW_RANGE = -4

} jpg_byteflow_ret_code_t;

struct byte_flow;

struct byte_flow* jpg_byteflow_allocate(const uint8_t *data, size_t len);

void jpg_byteflow_deallocate(struct byte_flow*);

jpg_byteflow_ret_code_t jpg_byteflow_get_next_byte(struct byte_flow* bf, uint8_t* out);

jpg_byteflow_ret_code_t jpg_byteflow_get_next_bytes_u16(struct byte_flow* bf, uint16_t* out);

jpg_byteflow_ret_code_t jpg_byteflow_get_next_n_bytes(struct byte_flow* bf, uint8_t* out, size_t n);

jpg_byteflow_ret_code_t jpg_byteflow_get_next_n_bytes_span(struct byte_flow* bf, const uint8_t** out, size_t n);

jpg_byteflow_ret_code_t jpg_byteflow_skip_next_n_bytes(struct byte_flow* bf, size_t n);

jpg_byteflow_ret_code_t jpg_byteflow_get_remain_len(struct byte_flow* bf, size_t* len);

jpg_byteflow_ret_code_t jpg_byteflow_get_next_n_bytes_subflow(struct byte_flow* bf, struct byte_flow** sbf, size_t n);

#endif /* PICTURE_LIB_JPG_BYTEFLOW_H */
