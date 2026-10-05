#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "jpg_bitflow.h"

struct jpg_bitflow_t{
    size_t curr_byte;
    size_t curr_bit;
    size_t bf_len;
    const uint8_t* bf;
};

static jpg_bitflow_ret_code_t _next_byte(struct jpg_bitflow_t* bf)
{
    /*  In accordance to ITU-T T.81 F.1.2.3 "Byte stuffing":
        "Whenever, in the course of normal encoding, the byte 
        value X’FF’ is created in the code string, a X’00’ 
        byte is stuffed into the code string."

        This means if decoder encounter byte pair 0xFF00 
        it must be handled as 0xFF and next 0x00 must be skipped. */

    if (!bf){
        return JPG_BITFLOW_BAD_ARG;
    }

    bf->curr_byte++;
    

    for(;;){
        if(bf->curr_byte >= bf->bf_len)
            return JPG_BITFLOW_END_OF_FLOW;

        if (bf->curr_byte > 0 &&
            bf->bf[bf->curr_byte] == 0x00 &&
            bf->bf[bf->curr_byte - 1] == 0xFF) {
            ++bf->curr_byte;
            continue;
        }

        if (bf->curr_byte + 1 < bf->bf_len &&
            bf->bf[bf->curr_byte] == 0xFF) {
            uint8_t marker = bf->bf[bf->curr_byte + 1];

            if (marker >= 0xD0 && marker <= 0xD7) {
                ++bf->curr_byte;
                return JPG_BITFLOW_FOUND_RST_MARKER;
            }

            if (marker == 0xD9) {
                ++bf->curr_byte;
                return JPG_BITFLOW_FOUND_EOI_MARKER;
            }
        }

        break;
    }
    
    bf->curr_bit = 0;
    if(bf->curr_byte >= bf->bf_len)
        return JPG_BITFLOW_END_OF_FLOW;

    return JPG_BITFLOW_RET_OK;
}

struct jpg_bitflow_t* jpg_bitflow_allocate(const uint8_t* flow, size_t len)
{
    if (!flow || !len)
        return NULL;

    struct jpg_bitflow_t* bf = (struct jpg_bitflow_t*)malloc(sizeof(struct jpg_bitflow_t));
    if(!bf)
        return NULL;

    bf->curr_bit = 0;
    bf->curr_byte = 0;
    bf->bf_len = len;
    bf->bf = flow;

    return bf;
}

jpg_bitflow_ret_code_t jpg_bitflow_deallocate(struct jpg_bitflow_t* bf)
{
    if (!bf)
        return JPG_BITFLOW_BAD_ARG;
    
    free(bf);

    return JPG_BITFLOW_RET_OK;
}

jpg_bitflow_ret_code_t jpg_bitflow_get_next_bit(struct jpg_bitflow_t* bf, int* pbit)
{
    if (!bf || !pbit)
        return JPG_BITFLOW_BAD_ARG;

    if (bf->curr_byte >= bf->bf_len)
        return JPG_BITFLOW_END_OF_FLOW;

    if(bf->curr_bit > 7){
        jpg_bitflow_ret_code_t ret = _next_byte(bf);
        if (ret != JPG_BITFLOW_RET_OK)
            return ret;
    }
    *pbit = (bf->bf[bf->curr_byte] & (1 << (7 - bf->curr_bit))) >> (7 - bf->curr_bit);

    ++bf->curr_bit; 

    return JPG_BITFLOW_RET_OK;
}

jpg_bitflow_ret_code_t jpg_bitflow_get_next_n_bits(struct jpg_bitflow_t* bf, uint8_t* pbuff, uint32_t n)
{
    if (!bf || !pbuff || !n)
        return JPG_BITFLOW_BAD_ARG;

    if (n > ((bf->bf_len - bf->curr_byte - 1) * 8 + (8 - bf->curr_bit)))
        return JPG_BITFLOW_RANGE;

    for (uint32_t i = 0; i < n; ++i){
        if (bf->curr_byte >= bf->bf_len)
            return JPG_BITFLOW_END_OF_FLOW;

        int flow_bit_val = false;
        jpg_bitflow_ret_code_t ret = jpg_bitflow_get_next_bit(bf, &flow_bit_val);
        if (ret != JPG_BITFLOW_RET_OK)
            return ret;

        uint32_t out_byte_num = i/8;
        uint32_t out_bit_num = i%8;

        pbuff[out_byte_num] = flow_bit_val ? 
                                pbuff[out_byte_num] | (1 << (7-out_bit_num)):
                                pbuff[out_byte_num] & ~(1 << (7-out_bit_num));
    }

    return JPG_BITFLOW_RET_OK;
}

jpg_bitflow_ret_code_t jpg_bitflow_get_next_i64_bits(struct jpg_bitflow_t* bf, int64_t* pbuff, uint32_t n)
{
    if (!bf || !pbuff || !n)
        return JPG_BITFLOW_BAD_ARG;

    if (n > (sizeof(*pbuff)*8))
        return JPG_BITFLOW_RANGE;

    *pbuff = 0;

    for (uint32_t i = 0; i < n; ++i){
        if (bf->curr_byte >= bf->bf_len)
            return JPG_BITFLOW_END_OF_FLOW;

        int flow_bit_val = false;
        jpg_bitflow_ret_code_t ret = jpg_bitflow_get_next_bit(bf, &flow_bit_val);
        if (ret != JPG_BITFLOW_RET_OK)
            return ret;

        *pbuff = (*pbuff << 1) | (int64_t)flow_bit_val;
    }

    return JPG_BITFLOW_RET_OK;
}