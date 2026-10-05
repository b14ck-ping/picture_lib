
#include "jpg_byteflow.h"
#include <stddef.h>
#include <stdlib.h>

struct byte_flow {
    const uint8_t *data;
    size_t size;
    size_t position;
};

struct byte_flow* jpg_byteflow_allocate(const uint8_t *data, size_t len)
{
    if(!data || !len)
        return NULL;

    struct byte_flow* bf = (struct byte_flow*)malloc(sizeof(struct byte_flow));
    if (!bf)
        return NULL;

    bf->data = data;
    bf->size = len;
    bf->position = 0;

    return bf;
}

void jpg_byteflow_deallocate(struct byte_flow* bf)
{
    if (bf)
        free(bf);
}

jpg_byteflow_ret_code_t jpg_byteflow_get_next_byte(struct byte_flow* bf, uint8_t* out)
{
    if (!bf || !out)
        return JPG_BYTEFLOW_BAD_ARG;

    if(bf->position >= bf->size)
        return JPG_BYTEFLOW_END_OF_FLOW;

    *out = bf->data[bf->position];
    ++bf->position;

    return JPG_BYTEFLOW_RET_OK;
}

jpg_byteflow_ret_code_t jpg_byteflow_get_next_bytes_u16(struct byte_flow* bf, uint16_t* out)
{
    if (!bf || !out)
        return JPG_BYTEFLOW_BAD_ARG;

    if(bf->position >= bf->size)
        return JPG_BYTEFLOW_END_OF_FLOW;

    if(bf->size - bf->position < 2ULL)
        return JPG_BYTEFLOW_RANGE;

    *out = 0x0000 | (bf->data[bf->position] << 8);
    *out |= bf->data[++bf->position];
    ++bf->position;

    return JPG_BYTEFLOW_RET_OK;
}

jpg_byteflow_ret_code_t jpg_byteflow_get_next_n_bytes(struct byte_flow* bf, uint8_t* out, size_t n)
{
    jpg_byteflow_ret_code_t ret_code = JPG_BYTEFLOW_RET_OK;
    if (!bf || !out || !n)
        return JPG_BYTEFLOW_BAD_ARG;

    if(bf->position >= bf->size)
        return JPG_BYTEFLOW_END_OF_FLOW;

    if(bf->size - bf->position < n)
        return JPG_BYTEFLOW_RANGE;

    for (size_t i = 0; i < n; ++i){
        if ((ret_code = jpg_byteflow_get_next_byte(bf, &out[i])) != JPG_BYTEFLOW_RET_OK)
            return ret_code;
    }

    return ret_code;
}

jpg_byteflow_ret_code_t jpg_byteflow_get_next_n_bytes_span(struct byte_flow* bf, const uint8_t** out, size_t n)
{
    if (!bf || !out || !n)
        return JPG_BYTEFLOW_BAD_ARG;

    if(bf->position >= bf->size)
        return JPG_BYTEFLOW_END_OF_FLOW;

    if(bf->size - bf->position < n)
        return JPG_BYTEFLOW_RANGE;

    *out = &bf->data[bf->position];

    return jpg_byteflow_skip_next_n_bytes(bf, n);
}

jpg_byteflow_ret_code_t jpg_byteflow_skip_next_n_bytes(struct byte_flow* bf, size_t n)
{
    jpg_byteflow_ret_code_t ret_code = JPG_BYTEFLOW_RET_OK;
    if (!bf || !n)
        return JPG_BYTEFLOW_BAD_ARG;

    if(bf->position >= bf->size)
        return JPG_BYTEFLOW_END_OF_FLOW;

    if(bf->size - bf->position < n)
        return JPG_BYTEFLOW_RANGE;

    bf->position += n;

    return ret_code;
}


jpg_byteflow_ret_code_t jpg_byteflow_get_remain_len(struct byte_flow* bf, size_t* len)
{
    if (!bf || !len)
        return JPG_BYTEFLOW_BAD_ARG;

    *len = bf->size - bf->position;

    return JPG_BYTEFLOW_RET_OK;
}

jpg_byteflow_ret_code_t jpg_byteflow_get_next_n_bytes_subflow(struct byte_flow* bf, struct byte_flow** sbf, size_t n)
{
    if (!bf || !sbf || !n)
        return JPG_BYTEFLOW_BAD_ARG;

    if(bf->position >= bf->size)
        return JPG_BYTEFLOW_END_OF_FLOW;

    if(bf->size - bf->position < n)
        return JPG_BYTEFLOW_RANGE;

    struct byte_flow* out_bf = jpg_byteflow_allocate(&bf->data[bf->position], n);
    if (!out_bf)
        return JPG_BYTEFLOW_NOMEM;

    *sbf = out_bf;

    return jpg_byteflow_skip_next_n_bytes(bf, n);
}