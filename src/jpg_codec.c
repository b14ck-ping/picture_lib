#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "jpg_codec.h"
#include "jpg_common.h"
#include "bmp_codec.h"
#include "DCT_coefficients.h"
#include "jpg_bitflow.h"
#include "jpg_byteflow.h"
#include "log.h"
#include <stdarg.h>


typedef int (*block_handler)(jpg_file_params_t *jpg_param, struct byte_flow* bf);

typedef enum chunk_types_codes {
    CHUNK_TYPE_DQT = 0xFFDB,
    CHUNK_TYPE_SOF0 = 0xFFC0,
    CHUNK_TYPE_DHT = 0xFFC4,
    CHUNK_TYPE_SOS = 0xFFDA,
    CHUNK_TYPE_COMMENT = 0xFFFE
} chunk_types_codes_t;

typedef struct {
    uint16_t type;
    block_handler handler;
} handlers_table_item_t;

static jpg_codec_ret_code_t chunk_handler_dqt(jpg_file_params_t *jpg_param, struct byte_flow* bf);
static jpg_codec_ret_code_t chunk_handler_sof0(jpg_file_params_t *jpg_param, struct byte_flow* bf);
static jpg_codec_ret_code_t chunk_handler_dht(jpg_file_params_t *jpg_param, struct byte_flow* bf);
static jpg_codec_ret_code_t chunk_handler_sos(jpg_file_params_t *jpg_param, struct byte_flow* bf);
static jpg_codec_ret_code_t chunk_handler_comment(jpg_file_params_t *jpg_param, struct byte_flow* bf);

static int decode_data_flow(jpg_decoding_params_t *decoding_param, int*** matrixes);

static handlers_table_item_t s_block_handlers[] = {
    {.type = CHUNK_TYPE_DQT, .handler = chunk_handler_dqt},
    {.type = CHUNK_TYPE_SOF0, .handler = chunk_handler_sof0},
    {.type = CHUNK_TYPE_DHT, .handler = chunk_handler_dht},
    {.type = CHUNK_TYPE_SOS, .handler = chunk_handler_sos},
    {.type = CHUNK_TYPE_COMMENT, .handler = chunk_handler_comment},
};

typedef enum {CHANNEL_Y = 1, CHANNEL_Cb, CHANNEL_Cr} channel_name_t;
typedef enum {COEFF_NAME_DC = 0, COEFF_NAME_AC} coeff_name_t;

static int jpeg_codec_call_section_handler(jpg_file_params_t* jpg_param, uint16_t section_marker, struct byte_flow* bf){
    for(int i = 0; i < sizeof(s_block_handlers)/sizeof(handlers_table_item_t); i++){
        if (s_block_handlers[i].type == section_marker && s_block_handlers[i].handler){
            return s_block_handlers[i].handler(jpg_param, bf);
        }
    }
    return JPG_CODEC_UNSUPPORTED_MARKER;
}

int jpeg_codec_zigzag_to_matrix(uint16_t **matrix_ptr, uint16_t * l_array, size_t arr_size)
{
    uint16_t *b_matrix = NULL;

    b_matrix = calloc(64, sizeof(uint16_t));

    int col = 0, row = 0;
    bool revers = false;
    b_matrix[0] = l_array[0];

    for (int i = 1 ; i < 64; i++){
        if ((row == 0 || row == 7)) {
            revers = row ? false : true;
            col++;
            b_matrix[8*row + col] = l_array[i];
            i++;
        } else if ((col == 7 || col == 0)){
            revers = col ? true : false;
            row++;
            b_matrix[8*row + col] = l_array[i];
            i++;
        }
        if (row == 7 && col == 7)
            break;
        if (revers){
            col = col-1 < 0 ? 0 : col-1;
            row = row+1 > 7 ? 7 : row+1;
            b_matrix[8*row + col] = l_array[i];
        } else {
            col = col+1 > 7 ? 7 : col+1;
            row = row-1 < 0 ? 0 : row-1;
            b_matrix[8*row + col] = l_array[i];
        }
    }

    if (matrix_ptr)
        *matrix_ptr = b_matrix;
    return 0;
}

int limit_value(int from, int to, int value)
{
    value = value < from ? from : value;
    value = value > to ? to : value;
    return value;
}

rgb_pixel_t s_YCbCr_to_RGB(uint8_t Y, uint8_t Cb, uint8_t Cr)
{
    rgb_pixel_t b_pixel = {};

    b_pixel.R = limit_value(0, 255, (int)((float)Y + 1.402*((float)Cr-128.0)));
    b_pixel.G = limit_value(0, 255, (int)((float)Y + 0.34414 * ((float)Cb-128.0) - 0.71414 * ((float)Cr-128.0)));
    b_pixel.B = limit_value(0, 255, (int)((float)Y + 1.772 * ((float)Cb-128.0)));

    return b_pixel;
}

int jpeg_codec_zigzag_to_matrix_int(int ***matrix_ptr, int * l_array, size_t arr_size)
{
    int **b_matrix = NULL;

    b_matrix = calloc(8, sizeof(int*));
    for (int i = 0 ; i < 8 ; i++){
        b_matrix[i] = calloc(8, sizeof(int*));
    }

    int col = 0, row = 0;
    bool revers = false;
    b_matrix[row][col] = l_array[0];

    for (int i = 1 ; i < 64; i++){
        if ((row == 0 || row == 7)) {
            revers = row ? false : true;
            col++;
            b_matrix[row][col] = l_array[i];
            i++;
        } else if ((col == 7 || col == 0)){
            revers = col ? true : false;
            row++;
            b_matrix[row][col] = l_array[i];
            i++;
        }
        if (row == 7 && col == 7)
            break;
        if (revers){
            col = col-1 < 0 ? 0 : col-1;
            row = row+1 > 7 ? 7 : row+1;
            b_matrix[row][col] = l_array[i];
        } else {
            col = col+1 > 7 ? 7 : col+1;
            row = row-1 < 0 ? 0 : row-1;
            b_matrix[row][col] = l_array[i];
        }
    }

    if (matrix_ptr)
        *matrix_ptr = b_matrix;
    return 0;
}

static int s_pow_2(int deg)
{
    int res = 1;
    for (int i = 0; i < deg; i++)
        res *= 2;
    return res;
}

int jpg_codec_file_dump(jpg_file_params_t *jpg_param)
{
    log_str(LOG_LEVEL_DEBUG, "=== Dump of jpg_file ===\n");
    log_str(LOG_LEVEL_DEBUG, "[DQT] Number of DQT tables: %d\n", jpg_param->dqt_tables_cnt);
    for (int i = 0; i < 4; ++i){
        if (!jpg_param->dqt_param[i].header.valid)
            continue;

        uint16_t *dqt_table = NULL;
        log_str(LOG_LEVEL_DEBUG, "[DQT] \tDQT table id: %d\n", jpg_param->dqt_param[i].header.tbl_id);
        log_str(LOG_LEVEL_DEBUG, "[DQT] \tDQT table value size: %d byte\n", jpg_param->dqt_param[i].header.tbl_value_prec);
        jpeg_codec_zigzag_to_matrix(&dqt_table, jpg_param->dqt_param[i].table, 64);
        log_str(LOG_LEVEL_DEBUG, "[DQT] \tDQT table:\n");
        for (int k = 0; k < 8; k++){
            log_str(LOG_LEVEL_DEBUG, 
                "[DQT] \t\t[");
            for (int j = 0; j < 8; j++){
                log_str(LOG_LEVEL_DEBUG, "%hx ", (uint8_t)dqt_table[8*k+j]);
            }
            log_str(LOG_LEVEL_DEBUG, "]\n");
        }
        free(dqt_table);
    }
    log_str(LOG_LEVEL_DEBUG, "[SOF0] Precision: %d\n", jpg_param->sof0->header.precision);
    log_str(LOG_LEVEL_DEBUG, "[SOF0] Image height: %d\n", jpg_param->sof0->header.height);
    log_str(LOG_LEVEL_DEBUG, "[SOF0] Image width: %d\n", jpg_param->sof0->header.width);
    log_str(LOG_LEVEL_DEBUG, "[SOF0] Number of channels: %d\n", jpg_param->sof0->header.channel_cnt);
    for (int i = 0; i < jpg_param->sof0->header.channel_cnt; i++){
        log_str(LOG_LEVEL_DEBUG, "[SOF0] \tChannel id: %d\n", jpg_param->sof0->channels[i].id);
        log_str(LOG_LEVEL_DEBUG, "[SOF0] \tHorizontal thinning: %d\n", jpg_param->sof0->channels[i].h_thinning);
        log_str(LOG_LEVEL_DEBUG, "[SOF0] \tVertical thinning: %d\n", jpg_param->sof0->channels[i].v_thinning);
        log_str(LOG_LEVEL_DEBUG, "[SOF0] \tDQT table id: %d\n\n", jpg_param->sof0->channels[i].dqt_id);
    }
    log_str(LOG_LEVEL_DEBUG, "[DHT] DHTs count: %d\n", jpg_param->dht_cnt);
    for (int i = 0; i < 2; i++){
        for (int l = 0; l < 2; l++){
            log_str(LOG_LEVEL_DEBUG, "[DHT] \tTable id: %d\n", jpg_param->dht[i][l].table_id);
            log_str(LOG_LEVEL_DEBUG, "[DHT] \tCLASS: %s\n", jpg_param->dht[i][l].table_class ? "AC" : "DC");
            log_str(LOG_LEVEL_DEBUG, "[DHT] \tCodes length counts:\n");
            log_str(LOG_LEVEL_DEBUG, "[DHT] \t");
            for (int j = 0; j < 16; j++){
                log_str(LOG_LEVEL_DEBUG, "[%d] ", jpg_param->dht[i][l].codes_cnts_by_length[j]);
            }
            log_str(LOG_LEVEL_DEBUG, "[DHT] \tCodes: \n");
            log_str(LOG_LEVEL_DEBUG, "[DHT] \t");
            for (int k = 0; k < jpg_param->dht[i][l].codes_value_cnt; k++){
                if (k%16 == 0)
                    log_str(LOG_LEVEL_DEBUG, "\n[DHT] \t");
                log_str(LOG_LEVEL_DEBUG, "[0x%.2X] ", jpg_param->dht[i][l].codes_value[k]);
            }
            log_str(LOG_LEVEL_DEBUG, "=========================================");
        }
    }

    log_str(LOG_LEVEL_DEBUG, "[SOS] Channels count: %d\n", jpg_param->sos.channel_cnt);
    for (int i = 0; i < jpg_param->sos.channel_cnt; i++){
        log_str(LOG_LEVEL_DEBUG, "[SOS] \tChannel id: %d\n", jpg_param->sos.channels[i].channel_id);
        log_str(LOG_LEVEL_DEBUG, "[SOS] \tHuffman DC table id: %d\n", jpg_param->sos.channels[i].huffman_table_dc_id);
        log_str(LOG_LEVEL_DEBUG, "[SOS] \tHuffman AC table id: %d\n", jpg_param->sos.channels[i].huffman_table_ac_id);
    }
    return 0;
}

int jpg_codec_jpg_param_remove(jpg_file_params_t *jpg_param)
{
    if (jpg_param->sof0)
        free(jpg_param->sof0);

    return 0;
}

static uint16_t *s_get_dqt(jpg_decoding_params_t *decode_param, int channel_id)
{
    // log_str("Search dqt for channel %d\n", channel_id);
    int chan_idx = 0;
    for (int i = 0; i < decode_param->sof0->header.channel_cnt; i++){
        if (decode_param->sof0->channels[i].id == channel_id)
            return decode_param->dqt_tables[decode_param->sof0->channels[i].dqt_id];
    }
    // log_str("Fail\n");
    return NULL;
}

jpg_codec_ret_code_t jpg_codec_file_decode(FILE *jpg_file, void **out_pixel_array)
{
    if (!jpg_file || !out_pixel_array){
        return JPG_CODEC_BAD_ARG;
    }

    fseek(jpg_file, 0, SEEK_END); 
    size_t len = ftell(jpg_file);
    if (!len)
        return JPG_CODEC_FILE_CORRUPTED;

    uint8_t* jpg_flow = (uint8_t*)malloc(len);
    if (!jpg_flow)
        return JPG_CODEC_NOMEM;

    fseek(jpg_file, 0, SEEK_SET);
    size_t itemsRead = fread(jpg_flow, 1, len, jpg_file);

    if (!itemsRead || itemsRead != len )
        return JPG_CODEC_FILE_CORRUPTED;

    jpg_codec_ret_code_t ret_code = jpg_codec_memory_decode(jpg_flow, len, out_pixel_array);
    free(jpg_flow);
    return ret_code;
}


jpg_codec_ret_code_t jpg_codec_memory_decode(const uint8_t *jpg_data, size_t len, void **out_pixel_array)
{
    jpg_file_params_t jpg_param = {};
    jpg_codec_ret_code_t ret_code = JPG_CODEC_RET_OK;
    struct byte_flow* bf = jpg_byteflow_allocate(jpg_data, len);
    clock_t begin = clock();

    uint16_t l_begin_marker = 0;
    if (jpg_byteflow_get_next_bytes_u16(bf, &l_begin_marker) != JPG_BYTEFLOW_RET_OK){
        ret_code = JPG_CODEC_FILE_CORRUPTED;
        goto ret_error;
    }

    if (l_begin_marker != 0xFFD8){
        ret_code = JPG_CODEC_FILE_CORRUPTED;
        goto ret_error;
    }

    uint16_t l_section_marker = 0;
    // parse jpg file
    for (;;){
        if(l_section_marker != CHUNK_TYPE_SOS){
            if (jpg_byteflow_get_next_bytes_u16(bf, &l_section_marker) != JPG_BYTEFLOW_RET_OK){
                ret_code = JPG_CODEC_FILE_CORRUPTED;
                goto ret_error;
            }

            uint16_t l_section_size = 0;
            if (jpg_byteflow_get_next_bytes_u16(bf, &l_section_size) != JPG_BYTEFLOW_RET_OK){
                ret_code = JPG_CODEC_FILE_CORRUPTED;
                goto ret_error;
            }

            if (!l_section_size)
                continue;

            l_section_size -= sizeof(l_section_size);
            struct byte_flow* sub_bf = NULL;
            if (jpg_byteflow_get_next_n_bytes_subflow(bf, &sub_bf, l_section_size) != JPG_BYTEFLOW_RET_OK){
                ret_code = JPG_CODEC_FILE_CORRUPTED;
                goto ret_error;
            }
            
            ret_code = jpeg_codec_call_section_handler(&jpg_param, l_section_marker, sub_bf);
            if (ret_code != JPG_CODEC_UNSUPPORTED_MARKER && ret_code != JPG_CODEC_RET_OK){
                jpg_byteflow_deallocate(sub_bf);
                ret_code = JPG_CODEC_FILE_CORRUPTED;
                goto ret_error;
            }
            jpg_byteflow_deallocate(sub_bf);
        } else  {
            size_t remain_size = 0;

            if (jpg_byteflow_get_remain_len(bf, &jpg_param.encoded_data_size) != JPG_BYTEFLOW_RET_OK ||
                jpg_byteflow_get_next_n_bytes_span(bf, &jpg_param.encoded_data, jpg_param.encoded_data_size) != JPG_BYTEFLOW_RET_OK){
                ret_code = JPG_CODEC_FILE_CORRUPTED;
                goto ret_error;
            }

            break;
        }
    }

#ifdef TIME_BENCHMARKING
        clock_t end = clock();
        double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
        log_str("File decoded in %f sec\n", time_spent);
        clock_t begin_block = clock();
#endif

    jpg_codec_file_dump(&jpg_param);
    jpg_decoding_params_t jpg_decode_params = {};
    jpg_decode_params.dqt_tables_cnt = jpg_param.dqt_tables_cnt;
    for (int i = 0; i < 4; i++){
        if (!jpg_param.dqt_param[i].header.valid)
            continue;
        uint16_t *dqt_table = NULL;
        jpeg_codec_zigzag_to_matrix(&dqt_table, jpg_param.dqt_param[jpg_param.dqt_param[i].header.tbl_id].table, 64);
        memcpy(jpg_decode_params.dqt_tables[jpg_param.dqt_param[i].header.tbl_id], dqt_table, 64*sizeof(uint16_t));
        free(dqt_table);
    }

    jpg_decode_params.huffman_trees = calloc(jpg_param.dht_cnt, sizeof(huffman_tree_t*));
    if (!jpg_decode_params.huffman_trees){
        ret_code = JPG_CODEC_NOMEM;
        goto ret_error;
    }
    jpg_decode_params.huffman_trees_cnt = jpg_param.dht_cnt;
    uint8_t tree_idx = 0;
    for(int i = 0; i < 2; i++){
        for(int j = 0; j < 4; j++){
            if (!jpg_param.dht[i][j].valid)
                continue;

            if (tree_idx >= jpg_decode_params.huffman_trees_cnt){
                ret_code = JPG_CODEC_FILE_CORRUPTED;
                goto ret_error;
            }

            jpg_decode_params.huffman_trees[tree_idx] = huffman_tree_create(jpg_param.dht[i][j].table_class, 
                                                jpg_param.dht[i][j].table_id,
                                                jpg_param.dht[i][j].codes_cnts_by_length, 
                                                jpg_param.dht[i][j].codes_value, 
                                                jpg_param.dht[i][j].codes_value_cnt);
            if (!jpg_decode_params.huffman_trees[tree_idx]){
                ret_code = JPG_CODEC_FILE_CORRUPTED;
                goto ret_error;
            }
                
            huffman_tree_dump(jpg_decode_params.huffman_trees[tree_idx]);
            tree_idx++;
        }
    }

    if (tree_idx != jpg_decode_params.huffman_trees_cnt){
        ret_code = JPG_CODEC_FILE_CORRUPTED;
        goto ret_error;
    }
    
    jpg_decode_params.sos = jpg_param.sos;

    size_t sof0_size = sizeof(jpg_param.sof0->header) + jpg_param.sof0->header.channel_cnt * sizeof(jpg_sof0_channel);
    jpg_decode_params.sof0 = calloc(1, sof0_size);
    memcpy(jpg_decode_params.sof0, jpg_param.sof0, sof0_size);

    uint8_t Hmax = 0, Vmax = 0;
    for (uint8_t i = 0; i < jpg_decode_params.sof0->header.channel_cnt; i++ ){
        Hmax = jpg_decode_params.sof0->channels[i].h_thinning > Hmax ? 
                            jpg_decode_params.sof0->channels[i].h_thinning : Hmax;
        Vmax = jpg_decode_params.sof0->channels[i].v_thinning > Vmax ? 
                            jpg_decode_params.sof0->channels[i].v_thinning : Vmax;
    }
    jpg_decode_params.Hmax = Hmax;
    jpg_decode_params.Vmax = Vmax;

    jpg_decode_params.encoded_data = jpg_param.encoded_data;
    jpg_decode_params.encoded_data_size = jpg_param.encoded_data_size;
    
    log_str(LOG_LEVEL_DEBUG, "bitflow size = %lu\n", jpg_decode_params.encoded_data_size);

    jpg_codec_jpg_param_remove(&jpg_param);

    // for (size_t i =0; i < jpg_decode_params.encoded_data_size; i++){
    //     char str[9] = {};
    //     print_binary_8bit(jpg_decode_params.encoded_data[i] , str);
    //     log_str(LOG_LEVEL_DEBUG, "%s|", str);
    // }
    // log_str(LOG_LEVEL_DEBUG, "============================");

    int** zigzag_matrixes = NULL;
    int matrix_cnt = decode_data_flow(&jpg_decode_params, &zigzag_matrixes);
    if (matrix_cnt < 1)
        return matrix_cnt;

    int ***matrix = calloc(matrix_cnt, sizeof(int **));
    for(int i = 0; i < matrix_cnt; i++){
        int **b_matrix = NULL;
        jpeg_codec_zigzag_to_matrix_int(&b_matrix, zigzag_matrixes[i], 64);
        matrix[i] = b_matrix;
    }
    
    log_str(LOG_LEVEL_DEBUG, "[*] Found %d DCT Matrices\n", matrix_cnt);
    for(int i = 0; i < matrix_cnt; i++){
        log_str(LOG_LEVEL_DEBUG, "[*] DCT Matrix #%d\n", i);
        for (int k = 0; k < 8; k++){
            log_str(LOG_LEVEL_DEBUG, "[*] \t\t[");
            for (int j = 0; j < 8; j++){
                log_str(LOG_LEVEL_DEBUG, "%d ", matrix[i][k][j]);
            }
            log_str(LOG_LEVEL_DEBUG, "]\n");
        }
    }

#ifdef TIME_BENCHMARKING
    end = clock();
    time_spent = (double)(end - begin_block) / CLOCKS_PER_SEC;
    log_str(LOG_LEVEL_DEBUG, "All decode params compued in %f sec\n", time_spent);
    begin_block = clock();
#endif

    // Recompute Y channel matrix
    uint8_t b_channels_cnt[3] = {jpg_decode_params.sof0->channels[CHANNEL_Y-1].h_thinning * jpg_decode_params.sof0->channels[CHANNEL_Y-1].v_thinning,
                                 jpg_decode_params.sof0->channels[CHANNEL_Cb-1].h_thinning * jpg_decode_params.sof0->channels[CHANNEL_Cb-1].v_thinning,
                                 jpg_decode_params.sof0->channels[CHANNEL_Cr-1].h_thinning * jpg_decode_params.sof0->channels[CHANNEL_Cr-1].v_thinning};



    int matrix_cur_cnt = 0;
    for (int i = 0; i < 3; i++)
    {
        if (b_channels_cnt[i] == 1){
            matrix_cur_cnt++;
            continue;
        }
        
        for (int j = 1; j < b_channels_cnt[i]; j++){
            matrix_cur_cnt++;
            matrix[matrix_cur_cnt][0][0] += matrix[matrix_cur_cnt-1][0][0];
        }
    }

    for(int i = 0; i < matrix_cnt; i++){
        log_str(LOG_LEVEL_DEBUG, "[*] Matrix #%d\n", i);
        for (int k = 0; k < 8; k++){
            log_str(LOG_LEVEL_DEBUG, "[*] \t\t[");
            for (int j = 0; j < 8; j++){
                log_str(LOG_LEVEL_DEBUG, "%d ", matrix[i][k][j]);
            }
            log_str(LOG_LEVEL_DEBUG, "]\n");
        }
    }

    // Let's quantize matrices
    int current_channel_cnt = 0;
    channel_name_t current_channel_id = CHANNEL_Y;
    for (int i = 0; i < matrix_cnt; i++)
    {
        uint16_t *dqt_table_b = s_get_dqt(&jpg_decode_params, current_channel_id);

        for (int k = 0; k < 8; k++){
            for (int j = 0; j < 8; j++){
                matrix[i][k][j] *= (uint8_t)dqt_table_b[8*k + j];
            }
        }
        if (++current_channel_cnt >= b_channels_cnt[current_channel_id-1]){
            if(++current_channel_id > 3)
                current_channel_id = CHANNEL_Y;
        }
    }

    for(int i = 0; i < matrix_cnt; i++){
        log_str(LOG_LEVEL_DEBUG, "[*] Quantize Matrix #%d\n", i);
        for (int k = 0; k < 8; k++){
            log_str(LOG_LEVEL_DEBUG, "[*] \t\t[");
            for (int j = 0; j < 8; j++){
                log_str(LOG_LEVEL_DEBUG, "%d ", matrix[i][k][j]);
            }
            log_str(LOG_LEVEL_DEBUG, "]\n");
        }
    }

    // DCT
    int ***output_matrix = calloc(matrix_cnt, sizeof(int **));
    for (int i = 0; i < matrix_cnt; i++)
    {
        output_matrix[i] = calloc(8, sizeof(int*));
        for (int j = 0 ; j < 8 ; j++){
            output_matrix[i][j] = calloc(8, sizeof(int*));
        }

        for (int u = 0; u < 8; u++){
            for (int v = 0; v < 8; v++){
                double b_value_out = 0;
                for (int x = 0; x < 8; x++){
                    double b_value_x = 0;
                    for (int y = 0; y < 8; y++){
                        // double C_u = (x == 0 ? (double)M_SQRT1_2 : (double)1.0);
                        // double C_v = (y == 0 ? (double)M_SQRT1_2 : (double)1.0);
                        // double cos_coeff = cos(((2.0*((double)u)+1)*((double)x)*((double)M_PI))/16.0) * 
                        //                     cos(((2.0*((double)v)+1)*((double)y)*((double)M_PI))/16.0);
                        // double b_value = (double)matrix[i][y][x] * C_u * C_v * cos_coeff;
                        double b_value = (double)matrix[i][y][x] * dct_coeff_matrices[u][v].coeff_matrix[y][x];
                        b_value_x += b_value;
                    }
                    b_value_out += b_value_x;
                }
                output_matrix[i][v][u] = limit_value(0, 255, (int)(b_value_out * 0.25)+ 128);
            }
        }
    }


    for(int i = 0; i < matrix_cnt; i++){
        log_str(LOG_LEVEL_DEBUG, "[*] YCbCr Matrix #%d\n", i);
        for (int k = 0; k < 8; k++){
            log_str(LOG_LEVEL_DEBUG, "[*] \t\t[");
            for (int j = 0; j < 8; j++){
                log_str(LOG_LEVEL_DEBUG, "%d ", output_matrix[i][k][j]);
            }
            log_str(LOG_LEVEL_DEBUG, "]\n");
        }
    }

#ifdef TIME_BENCHMARKING    
    end = clock();
    time_spent = (double)(end - begin_block) / CLOCKS_PER_SEC;
    log_str(LOG_LEVEL_DEBUG, "DCT made in  %f sec\n", time_spent);
    begin_block = clock();
#endif

    log_str(LOG_LEVEL_DEBUG, "[*] Lets transform YCrCb to RGB.\n");
    size_t pixel_cnt = jpg_decode_params.sof0->header.height*jpg_decode_params.sof0->header.width;
    rgb_pixel_t **RGB_matrix = calloc(jpg_decode_params.sof0->header.height, sizeof(rgb_pixel_t **));
    for (int j = 0 ; j < jpg_decode_params.sof0->header.height ; j++){
            RGB_matrix[j] = calloc(jpg_decode_params.sof0->header.width, sizeof(rgb_pixel_t*));
    }

    for (size_t k = 0; k < jpg_decode_params.sof0->header.height; k++){
        for (size_t j = 0; j < jpg_decode_params.sof0->header.width; j++){
            size_t cur_matrix_block = 6*(j/16 + (k/16 > 0 ? (jpg_decode_params.sof0->header.width/16) * (k/16) : 0 ));
            size_t cur_matrix_Y = 0;
            size_t cur_row = k%16;
            size_t cur_col = j%16;
            size_t cur_row_Y = cur_row;
            size_t cur_col_Y = cur_col;
            if (cur_row <= 7 && cur_col > 7){
                cur_col_Y -= 8;
                cur_matrix_Y = 1;
            }else if (cur_row > 7 && cur_col <= 7){
                cur_matrix_Y = 2;
                cur_row_Y -= 8;
            }else if (cur_row > 7 && cur_col > 7){
                cur_col_Y -= 8;
                cur_row_Y -= 8;
                cur_matrix_Y = 3;
            }
                

            RGB_matrix[k][j] = s_YCbCr_to_RGB((uint8_t)output_matrix[cur_matrix_block + cur_matrix_Y][cur_row_Y][cur_col_Y],
                                                                                    (uint8_t)output_matrix[cur_matrix_block+4][cur_row/2][cur_col/2], 
                                                                                    (uint8_t)output_matrix[cur_matrix_block+5][cur_row/2][cur_col/2]);
        }
    }

#ifdef TIME_BENCHMARKING
    end = clock();
    time_spent = (double)(end - begin_block) / CLOCKS_PER_SEC;
    log_str(LOG_LEVEL_DEBUG, "All RGB pixels filled in  %f sec\n", time_spent);
    begin_block = clock();
#endif

    log_str(LOG_LEVEL_DEBUG, "[*] RGB Matrices\n");
    for (int k = 0; k < 16; k++){
        log_str(LOG_LEVEL_DEBUG, "[*] \t\t[");
        for (int j = 0; j < 16; j++){
            log_str(LOG_LEVEL_DEBUG, "%d ", RGB_matrix[k][j].R);
        }
        log_str(LOG_LEVEL_DEBUG, "]\n");
    }
    log_str(LOG_LEVEL_DEBUG, "================================");
    for (int k = 0; k < 16; k++){
        log_str(LOG_LEVEL_DEBUG, "[*] \t\t[");
        for (int j = 0; j < 16; j++){
            log_str(LOG_LEVEL_DEBUG, "%d ", RGB_matrix[k][j].G);
        }
        log_str(LOG_LEVEL_DEBUG, "]\n");
    }
    log_str(LOG_LEVEL_DEBUG, "\n");
    for (int k = 0; k < 16; k++){
        log_str(LOG_LEVEL_DEBUG, "[*] \t\t[");
        for (int j = 0; j < 16; j++){
            log_str(LOG_LEVEL_DEBUG, "%d ", RGB_matrix[k][j].B);
        }
        log_str(LOG_LEVEL_DEBUG, "]\n");
    }

#ifdef TIME_BENCHMARKING
    end = clock();
    time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    log_str(LOG_LEVEL_DEBUG, "File decoded in %f sec\n", time_spent);
#endif

    return bmp_file_create(RGB_matrix, jpg_decode_params.sof0->header.height, jpg_decode_params.sof0->header.width);
    
ret_error:
    jpg_codec_jpg_param_remove(&jpg_param);
    return ret_code;
}


static jpg_codec_ret_code_t chunk_handler_dqt(jpg_file_params_t *jpg_param, struct byte_flow* bf)
{
    size_t remain_bf_len = 0;
    uint8_t l_buf_params = 0;

    if (!bf || !jpg_param)
        return JPG_CODEC_BAD_ARG;

    do{
        if (jpg_byteflow_get_next_byte(bf, &l_buf_params) != JPG_BYTEFLOW_RET_OK)
            return JPG_CODEC_FILE_CORRUPTED;

        uint8_t tbl_id = (int)(l_buf_params & 0x0F);
        if (tbl_id > 3 || jpg_param->dqt_param[tbl_id].header.valid)
            return JPG_CODEC_FILE_CORRUPTED;

        uint8_t prec = (int)((l_buf_params & 0xF0) >> 4);
        if (prec > 1)
            return JPG_CODEC_FILE_CORRUPTED;
        
        // Check remain len
        size_t data_len = 64 * (prec + 1);
        
        if (jpg_byteflow_get_remain_len(bf, &remain_bf_len) != JPG_BYTEFLOW_RET_OK || 
             remain_bf_len < data_len)
            return JPG_CODEC_FILE_CORRUPTED;

        jpg_dqt_t* p_table = &jpg_param->dqt_param[tbl_id];
        p_table->header.tbl_value_prec = prec;
        jpg_param->dqt_param[tbl_id].header.tbl_id = tbl_id;
        
        if (p_table->header.tbl_value_prec == DQT_PRECISION_8_BIT){
            for (size_t i = 0; i < 64; ++i){
                uint8_t l_tbl_val = 0;
                if (jpg_byteflow_get_next_byte(bf, &l_tbl_val) != JPG_BYTEFLOW_RET_OK)
                    return JPG_CODEC_FILE_CORRUPTED;

                p_table->table[i] = (uint16_t)l_tbl_val;
            }
        } else {
            for (size_t i = 0; i < 64; ++i){
                uint16_t l_tbl_val = 0;
                if (jpg_byteflow_get_next_bytes_u16(bf, &l_tbl_val) != JPG_BYTEFLOW_RET_OK)
                    return JPG_CODEC_FILE_CORRUPTED;

                p_table->table[i] = l_tbl_val;
            }
        }

        p_table->header.valid = true;
        jpg_param->dqt_tables_cnt++;

        if (jpg_byteflow_get_remain_len(bf, &remain_bf_len) != JPG_BYTEFLOW_RET_OK)
            return JPG_CODEC_FILE_CORRUPTED;
    }while(remain_bf_len);

    
    return JPG_CODEC_RET_OK;
}


static jpg_codec_ret_code_t chunk_handler_sof0(jpg_file_params_t *jpg_param, struct byte_flow* bf)
{
    if (!bf || !jpg_param)
        return JPG_CODEC_BAD_ARG;

    uint8_t precision = 0, chanel_cnt = 0;
    uint16_t height = 0, width = 0;
    if (jpg_byteflow_get_next_byte(bf, &precision) != JPG_BYTEFLOW_RET_OK)
        return JPG_CODEC_FILE_CORRUPTED;
    if (jpg_byteflow_get_next_bytes_u16(bf, &height) != JPG_BYTEFLOW_RET_OK)
        return JPG_CODEC_FILE_CORRUPTED;
    if (jpg_byteflow_get_next_bytes_u16(bf, &width) != JPG_BYTEFLOW_RET_OK)
        return JPG_CODEC_FILE_CORRUPTED;
    if (jpg_byteflow_get_next_byte(bf, &chanel_cnt) != JPG_BYTEFLOW_RET_OK)
        return JPG_CODEC_FILE_CORRUPTED;

    jpg_sof0_t *l_buf_sof = calloc(sizeof(l_buf_sof->header) + chanel_cnt * sizeof(jpg_sof0_channel), 1);
    l_buf_sof->header.precision = precision;
    l_buf_sof->header.height = height;
    l_buf_sof->header.width = width;
    l_buf_sof->header.channel_cnt = chanel_cnt;

    for (int i = 0; i < chanel_cnt; i++){
        if (jpg_byteflow_get_next_byte(bf, &l_buf_sof->channels[i].id) != JPG_BYTEFLOW_RET_OK)
            return JPG_CODEC_FILE_CORRUPTED;

        uint8_t l_thin = 0;
        if (jpg_byteflow_get_next_byte(bf, &l_thin) != JPG_BYTEFLOW_RET_OK)
            return JPG_CODEC_FILE_CORRUPTED;

        l_buf_sof->channels[i].h_thinning = ((l_thin & 0xF0) >> 4);
        l_buf_sof->channels[i].v_thinning = l_thin & 0x0F;

        if (jpg_byteflow_get_next_byte(bf, &l_buf_sof->channels[i].dqt_id) != JPG_BYTEFLOW_RET_OK)
            return JPG_CODEC_FILE_CORRUPTED;
    }

    jpg_param->sof0 = l_buf_sof;

    return JPG_CODEC_RET_OK;
}

static jpg_codec_ret_code_t chunk_handler_dht(jpg_file_params_t *jpg_param, struct byte_flow* bf)
{
    if (!bf || !jpg_param)
        return JPG_CODEC_BAD_ARG;

    size_t l_remain_len = 0;
    do{
        uint8_t l_tbl_param = 0;
        uint16_t l_codes_val_cnt = 0;
        if (jpg_byteflow_get_next_byte(bf, &l_tbl_param) != JPG_BYTEFLOW_RET_OK)
            return JPG_CODEC_FILE_CORRUPTED;

        uint8_t table_class = (l_tbl_param & 0xF0) >> 4;
        uint8_t table_id = l_tbl_param & 0x0F;

        if (table_class > 1 || table_id > 3)
            return JPG_CODEC_FILE_CORRUPTED;

        jpg_dht_t l_dht = {0};

        if (jpg_byteflow_get_next_n_bytes(bf, l_dht.codes_cnts_by_length, 16) != JPG_BYTEFLOW_RET_OK)
            return JPG_CODEC_FILE_CORRUPTED;

        for(int i = 0; i < 16; i++){
            l_codes_val_cnt += l_dht.codes_cnts_by_length[i];
        }

        if(l_codes_val_cnt > 256 || !l_codes_val_cnt)
            return JPG_CODEC_FILE_CORRUPTED;

        if (jpg_byteflow_get_next_n_bytes(bf, l_dht.codes_value, l_codes_val_cnt) != JPG_BYTEFLOW_RET_OK)
            return JPG_CODEC_FILE_CORRUPTED;

        l_dht.table_id = table_id;
        l_dht.table_class = table_class;
        l_dht.codes_value_cnt = l_codes_val_cnt;
        l_dht.valid = true;

        if(!jpg_param->dht[table_class][table_id].valid)
            ++jpg_param->dht_cnt;
            
        jpg_param->dht[table_class][table_id] = l_dht;
        
        if (jpg_byteflow_get_remain_len(bf, &l_remain_len) != JPG_BYTEFLOW_RET_OK)
            return JPG_CODEC_FILE_CORRUPTED;
    }while(l_remain_len);

    return JPG_CODEC_RET_OK;
}

static jpg_codec_ret_code_t chunk_handler_sos(jpg_file_params_t *jpg_param, struct byte_flow* bf)
{
    if (!bf || !jpg_param)
        return JPG_CODEC_BAD_ARG;

    uint8_t l_channels_cnt = 0;
    if ((jpg_byteflow_get_next_byte(bf, &l_channels_cnt) != JPG_BYTEFLOW_RET_OK) || !l_channels_cnt || l_channels_cnt > 4)
        return JPG_CODEC_FILE_CORRUPTED;

    jpg_sos_t l_buf_sos = {0};
    l_buf_sos.channel_cnt = l_channels_cnt;

    for(int i = 0; i < l_channels_cnt; i++){
        uint8_t channel_id = 0;
        if ((jpg_byteflow_get_next_byte(bf, &channel_id) != JPG_BYTEFLOW_RET_OK))
            return JPG_CODEC_FILE_CORRUPTED;

        if (!jpg_param->sof0)
            return JPG_CODEC_FILE_CORRUPTED;

        bool is_found = 0;
        for (int i = 0; i < jpg_param->sof0->header.channel_cnt; ++i){
            if (jpg_param->sof0->channels[i].id == channel_id){
                is_found = true;
                break;
            }
        }
        if (!is_found)
            return JPG_CODEC_FILE_CORRUPTED;

        for(int j = 0; j < i; j++){
            if (l_buf_sos.channels[j].channel_id == channel_id)
                return JPG_CODEC_FILE_CORRUPTED;
        }

        l_buf_sos.channels[i].channel_id = channel_id;

        uint8_t l_ids = 0;
        if ((jpg_byteflow_get_next_byte(bf, &l_ids) != JPG_BYTEFLOW_RET_OK))
            return JPG_CODEC_FILE_CORRUPTED;

        uint8_t dc_id = (l_ids & 0xF0) >> 4, ac_id = l_ids & 0x0F;
        if (dc_id > 3 || ac_id > 3)
            return JPG_CODEC_FILE_CORRUPTED;

        l_buf_sos.channels[i].huffman_table_dc_id = dc_id;
        l_buf_sos.channels[i].huffman_table_ac_id = ac_id;
    }

    if ((jpg_byteflow_get_next_byte(bf, &l_buf_sos.start_sps) != JPG_BYTEFLOW_RET_OK))
        return JPG_CODEC_FILE_CORRUPTED;

    if ((jpg_byteflow_get_next_byte(bf, &l_buf_sos.end_sps) != JPG_BYTEFLOW_RET_OK))
        return JPG_CODEC_FILE_CORRUPTED;
    
    uint8_t sabs = 0;
    if ((jpg_byteflow_get_next_byte(bf, &sabs) != JPG_BYTEFLOW_RET_OK))
        return JPG_CODEC_FILE_CORRUPTED;

    l_buf_sos.sab_h = (sabs & 0xF0) >> 4;
    l_buf_sos.sab_l = sabs & 0x0F;

    // ITU-T T.81 B.2.3 Scan header syntax (Table B.3)
    if (l_buf_sos.start_sps || l_buf_sos.end_sps != 63 ||
        l_buf_sos.sab_h || l_buf_sos.sab_l)
        return JPG_CODEC_FILE_CORRUPTED;

    size_t len = 0;
    if (jpg_byteflow_get_remain_len(bf, &len) != JPG_BYTEFLOW_RET_OK || len != 0)
        return JPG_CODEC_FILE_CORRUPTED;

    l_buf_sos.valid = true;
    jpg_param->sos = l_buf_sos;
    return JPG_CODEC_RET_OK;
}

static jpg_codec_ret_code_t chunk_handler_comment(jpg_file_params_t *jpg_param, struct byte_flow* bf)
{
    if (!bf || !jpg_param)
        return -1;

    size_t len = 0;
    if (jpg_byteflow_get_remain_len(bf, &len) != JPG_BYTEFLOW_RET_OK)
        return JPG_CODEC_FILE_CORRUPTED;

    char *l_comment = (char*)calloc(len + 1, sizeof(uint8_t));
    if (jpg_byteflow_get_next_n_bytes(bf, (uint8_t*)l_comment, len))
        return JPG_CODEC_FILE_CORRUPTED;

    log_str(LOG_LEVEL_DEBUG, "Comment: %s\n", (char*)l_comment);
    free(l_comment);

    return 0;
}

static huffman_tree_t *s_get_dht(jpg_decoding_params_t *decoding_param, channel_name_t current_channel,
                                coeff_name_t current_coeff)
{
    huffman_tree_t **huffman_trees = decoding_param->huffman_trees;
    int huffman_trees_cnt = decoding_param->huffman_trees_cnt;

    uint8_t channel_idx = 0;
    bool is_found = false;
    for (int i = 0; i < decoding_param->sos.channel_cnt; i++){
        if (decoding_param->sos.channels[i].channel_id == current_channel){
            channel_idx = i;
            is_found = true;
            break;
        }
    }

    if (!is_found)
        return NULL;
    
    uint8_t tree_id = 0;
    if (current_coeff == COEFF_NAME_AC)
        tree_id = decoding_param->sos.channels[channel_idx].huffman_table_ac_id;
    else
        tree_id = decoding_param->sos.channels[channel_idx].huffman_table_dc_id;

    // log_str("Search table with id = %d and tree class = %d\n", tree_id, current_coeff);
    for (int i = 0; i < huffman_trees_cnt; i++){
        if (huffman_trees[i] && (huffman_trees[i]->id == tree_id) && 
                    (huffman_trees[i]->tree_class == (uint8_t)current_coeff)){
            return huffman_trees[i];
        }    
    }
    // log_str("Fail\n");
    return NULL;
}

static int decode_data_flow(jpg_decoding_params_t *decoding_param, int*** zigzag_matrix)
{
    int ret_code = 0;
    int** l_zigzag_matrixes = (int**)calloc(1, sizeof(int*));
    l_zigzag_matrixes[0] = (int*)calloc(64, sizeof(int));
    size_t current_l_zigzag_matrix_cnt = 0;
    uint8_t b_zigzag_pos = 0;

    uint8_t b_channels_cnt[3] = {[CHANNEL_Y-1] = decoding_param->sof0->channels[CHANNEL_Y-1].h_thinning * decoding_param->sof0->channels[CHANNEL_Y-1].v_thinning,
                                 [CHANNEL_Cb-1] = decoding_param->sof0->channels[CHANNEL_Cb-1].h_thinning * decoding_param->sof0->channels[CHANNEL_Cb-1].v_thinning,
                                 [CHANNEL_Cr-1] = decoding_param->sof0->channels[CHANNEL_Cr-1].h_thinning * decoding_param->sof0->channels[CHANNEL_Cr-1].v_thinning};

    coeff_name_t current_coeff = COEFF_NAME_DC;
    channel_name_t current_channel = CHANNEL_Y;
    int current_channel_cnt = 0;

    huffman_tree_t *huffman_tree_current = s_get_dht(decoding_param, current_channel, current_coeff);
    if (!huffman_tree_current){
        return JPG_CODEC_NULLPTR_ERR;
    } 
    tree_node_t *current_node = huffman_tree_current->tree;
    uint8_t current_value = 0;

    struct jpg_bitflow_t* bf = jpg_bitflow_allocate(decoding_param->encoded_data, decoding_param->encoded_data_size);
    if (!bf){
        log_str(LOG_LEVEL_ERROR, "Memory allocation error\n");
        return JPG_CODEC_NOMEM;
    }
    size_t expected_matrix_cnt = 0;
    size_t MCU_x = ceil(decoding_param->sof0->header.width  / (8.0 * decoding_param->Hmax));
    size_t MCU_y = ceil(decoding_param->sof0->header.height  / (8.0 * decoding_param->Vmax));
    for (uint8_t i = 0; i < decoding_param->sof0->header.channel_cnt; i++ ){
        expected_matrix_cnt += decoding_param->sof0->channels[i].h_thinning * decoding_param->sof0->channels[i].v_thinning;
    }
    
    expected_matrix_cnt *= MCU_x * MCU_y;
    for (;;){
        if(current_l_zigzag_matrix_cnt == expected_matrix_cnt)
            break; 

        if (current_node && !current_node->leaf_node){
            int curr_bit = 0;
            ret_code = jpg_bitflow_get_next_bit(bf, &curr_bit);
            if (ret_code == JPG_BITFLOW_FOUND_EOI_MARKER || ret_code == JPG_BITFLOW_END_OF_FLOW){
                if (current_l_zigzag_matrix_cnt < expected_matrix_cnt){
                    ret_code = JPG_CODEC_FILE_CORRUPTED;
                    goto ret_error;
                } else 
                    break;
            } 

            if (ret_code == JPG_BITFLOW_FOUND_RST_MARKER){
                ret_code = JPG_CODEC_UNSUPPORTED_MARKER;
                goto ret_error;
            }
                
            if (ret_code != JPG_BITFLOW_RET_OK)
                goto ret_error;

            current_node = curr_bit ? (current_node->right) : (current_node->left);
            if (!current_node){
                ret_code = JPG_CODEC_NULLPTR_ERR;
                goto ret_error;
            }
            continue;
        } 

        if(current_node->value == 0){
            if (current_coeff == COEFF_NAME_DC){
                l_zigzag_matrixes[current_l_zigzag_matrix_cnt][b_zigzag_pos] = 0;
                // log_str(LOG_LEVEL_DEBUG, "dc coeff_value = 0\n");
                b_zigzag_pos++;
                current_coeff = COEFF_NAME_AC;
                huffman_tree_current = s_get_dht(decoding_param, current_channel, current_coeff);
                if (!huffman_tree_current){
                    ret_code = JPG_CODEC_NULLPTR_ERR;
                    goto ret_error;
                } 
                current_node = huffman_tree_current->tree;   
            }  else {
                // log_str(LOG_LEVEL_DEBUG, "set next ac coeff_value = 0\n");
                current_l_zigzag_matrix_cnt++;
                if(current_l_zigzag_matrix_cnt == expected_matrix_cnt)
                    break;
                b_zigzag_pos = 0;
                l_zigzag_matrixes = (int**)realloc(l_zigzag_matrixes, (current_l_zigzag_matrix_cnt + 1) * sizeof(int*));
                l_zigzag_matrixes[current_l_zigzag_matrix_cnt] = (int*)calloc(64, sizeof(int));
                current_coeff = COEFF_NAME_DC;
                current_channel_cnt++;
                if (current_channel_cnt >= b_channels_cnt[current_channel-1]){
                    current_channel++;
                    if(current_channel > CHANNEL_Cr){
                        current_channel = CHANNEL_Y;
                        current_channel_cnt = 0;
                    }
                }
                // log_str(LOG_LEVEL_DEBUG, "Set channel = %d\n", current_channel);
                huffman_tree_current = s_get_dht(decoding_param, current_channel, current_coeff);
                if (!huffman_tree_current){
                    ret_code = JPG_CODEC_NULLPTR_ERR;
                    goto ret_error;
                } 
                current_node = huffman_tree_current->tree; 
            }                                
        } else {
            if (current_coeff == COEFF_NAME_DC){
                int64_t coeff_value = 0;
                ret_code = jpg_bitflow_get_next_i64_bits(bf, &coeff_value, current_node->value);
                if (ret_code == JPG_BITFLOW_FOUND_EOI_MARKER){
                    if (current_l_zigzag_matrix_cnt < expected_matrix_cnt){
                        ret_code = JPG_CODEC_FILE_CORRUPTED;
                        goto ret_error;
                    } else 
                        break;
                } 

                if (ret_code == JPG_BITFLOW_FOUND_RST_MARKER){
                    ret_code = JPG_CODEC_UNSUPPORTED_MARKER;
                    goto ret_error;
                }
                if(ret_code != JPG_BITFLOW_RET_OK)
                    goto ret_error;
                // log_str(LOG_LEVEL_DEBUG, "dc coeff_value = %d\n", coeff_value);
                l_zigzag_matrixes[current_l_zigzag_matrix_cnt][b_zigzag_pos] = (coeff_value & (1 << (current_node->value-1))) ? coeff_value : coeff_value - s_pow_2(current_node->value) + 1;
                b_zigzag_pos++;
                current_coeff = COEFF_NAME_AC;
                huffman_tree_current = s_get_dht(decoding_param, current_channel, current_coeff);
                if (!huffman_tree_current){
                    ret_code = JPG_CODEC_NULLPTR_ERR;
                    goto ret_error;
                } 
                current_node = huffman_tree_current->tree; 
            } else {
                int64_t coeff_value = 0;
                uint8_t zero_cnt = (current_node->value & 0xF0) >> 4;
                uint16_t coef_lng = (current_node->value & 0x0F);
                if (current_node->value == 0xF0) {
                    if ((size_t)b_zigzag_pos + 16 > 64) {
                        ret_code = JPG_CODEC_FILE_CORRUPTED;
                        goto ret_error;
                    }

                    b_zigzag_pos += 16;
                } else {
                    if (coef_lng == 0) {
                        ret_code = JPG_CODEC_FILE_CORRUPTED;
                        goto ret_error;
                    }

                    if ((size_t)b_zigzag_pos + zero_cnt >= 64) {
                        ret_code = JPG_CODEC_FILE_CORRUPTED;
                        goto ret_error;
                    }

                    b_zigzag_pos += zero_cnt;

                    ret_code = jpg_bitflow_get_next_i64_bits(bf, &coeff_value, coef_lng);
                    if (ret_code == JPG_BITFLOW_FOUND_EOI_MARKER){
                        if (current_l_zigzag_matrix_cnt < expected_matrix_cnt){
                            ret_code = JPG_CODEC_FILE_CORRUPTED;
                            goto ret_error;
                        } else 
                            break;
                    } 

                    if (ret_code == JPG_BITFLOW_FOUND_RST_MARKER){
                        ret_code = JPG_CODEC_UNSUPPORTED_MARKER;
                        goto ret_error;
                    }

                    if(ret_code != JPG_BITFLOW_RET_OK)
                        goto ret_error;

                    l_zigzag_matrixes[current_l_zigzag_matrix_cnt][b_zigzag_pos] =
                        (coeff_value & (INT64_C(1) << (coef_lng - 1)))
                            ? coeff_value
                            : coeff_value + 1 - s_pow_2(coef_lng);

                    b_zigzag_pos++;
                }
                // if (zero_cnt) log_str(LOG_LEVEL_DEBUG, "skip %d ac coeff remain zero.\n", zero_cnt);
                if (b_zigzag_pos > 63){
                    // log_str(LOG_LEVEL_DEBUG, "end of matrix\n");
                    current_l_zigzag_matrix_cnt++;
                    if(current_l_zigzag_matrix_cnt == expected_matrix_cnt)
                        break;
                    b_zigzag_pos = 0;
                    l_zigzag_matrixes = (int**)realloc(l_zigzag_matrixes, (current_l_zigzag_matrix_cnt + 1)* sizeof(int*));
                    l_zigzag_matrixes[current_l_zigzag_matrix_cnt] = (int*)calloc(64, sizeof(int));
                    current_coeff = COEFF_NAME_DC;
                    current_channel_cnt++;
                    if (current_channel_cnt >= b_channels_cnt[current_channel-1]){
                        current_channel++;
                        if(current_channel > CHANNEL_Cr){
                            current_channel = CHANNEL_Y;
                            current_channel_cnt = 0;
                        }
                    }
                    huffman_tree_current = s_get_dht(decoding_param, current_channel, current_coeff);
                    if (!huffman_tree_current){
                        ret_code = JPG_CODEC_NULLPTR_ERR;
                        goto ret_error;
                    }            
                } 
                current_node = huffman_tree_current->tree;
            }
        }
    }

    if (zigzag_matrix)
        *zigzag_matrix = l_zigzag_matrixes;

    jpg_bitflow_deallocate(bf);    
    return current_l_zigzag_matrix_cnt;

ret_error:
    jpg_bitflow_deallocate(bf);
    return ret_code;
}