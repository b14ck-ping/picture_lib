#ifndef PICTURE_LIB_JPG_COMMON_H
#define PICTURE_LIB_JPG_COMMON_H

#include "huffman.h"
#include <stdint.h>

// #define TIME_BENCHMARKING
typedef struct jpg_comment {
    uint16_t size;
    char data[];
}jpg_comment_t;

typedef enum {
    DQT_PRECISION_8_BIT = 0,
    DQT_PRECISION_16_BIT
} DQT_precision_t;
typedef struct jpg_dqt {
    struct {
        bool valid;
        DQT_precision_t tbl_value_prec;
        int tbl_id;
    } header;
    uint16_t table[64];
}jpg_dqt_t;

typedef struct {

} dqt_table;

typedef struct jpg_sof0_channel{
    uint8_t id;
    uint8_t h_thinning;
    uint8_t v_thinning;
    uint8_t dqt_id;
}jpg_sof0_channel;

typedef struct jpg_sof0 {
    struct{
        uint8_t precision;
        uint16_t height;
        uint16_t width;
        uint8_t channel_cnt;
    } header;
    jpg_sof0_channel channels[];
}jpg_sof0_t;

typedef struct {
    bool valid;
    uint8_t table_class;
    uint8_t table_id;
    uint8_t codes_cnts_by_length[16];
    uint16_t codes_value_cnt;
    uint8_t codes_value[256];
} jpg_dht_t;

typedef struct {
    uint8_t channel_id;
    uint8_t huffman_table_dc_id;
    uint8_t huffman_table_ac_id;
} jpg_sos_channel_t;

typedef struct {
    uint8_t channel_cnt;
    bool valid;
    jpg_sos_channel_t channels[4];
    uint8_t start_sps;//Start of spectral or predictor selection
    uint8_t end_sps;//End of spectral selection
    uint8_t sab_h;// Successive approximation bit position high
    uint8_t sab_l;// Successive approximation bit position high
} jpg_sos_t;

typedef struct {
    uint8_t R;
    uint8_t G;
    uint8_t B;
} rgb_pixel_t;
    
typedef struct {

} YCbCr_pixel;

typedef struct {

} channels_matrixes_t;

typedef struct jpg_file_params {
    uint8_t dqt_tables_cnt;
    jpg_dqt_t dqt_param[4]; // ITU-T T.81 B.2.4.1 Quantization table-specification syntax (Table B.4)
    jpg_sof0_t *sof0;
    int dht_cnt;
    jpg_dht_t dht[2][4]; // ITU-T T.81 B.2.4.2 Huffman table-specification syntax (Table B.5) 
                      // There can be 8 tables - id 0..3 for each class (DC or AC)
    jpg_sos_t sos; // ITU-T T.81 B.2.3 Scan header syntax (Table B.3)
    size_t encoded_data_size;
    const uint8_t *encoded_data;
} jpg_file_params_t;

typedef struct jpg_decoding_params {
    uint8_t Hmax;
    uint8_t Vmax;
    int dqt_tables_cnt;
    uint16_t dqt_tables[4][64];
    jpg_sos_t sos;
    jpg_sof0_t *sof0;
    int huffman_trees_cnt;
    huffman_tree_t **huffman_trees;
    size_t encoded_data_size;
    const uint8_t *encoded_data;
} jpg_decoding_params_t;


#endif /* PICTURE_LIB_JPG_COMMON_H */
