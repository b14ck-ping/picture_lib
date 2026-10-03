#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "jpg_codec.h"
#include "jpg_common.h"
#include "bmp_codec.h"
#include "DCT_coefficients.h"
#include "jpg_bitflow.h"
#include <stdarg.h>


typedef int (*block_handler)(jpg_file_params_t *jpg_param, void* data, uint16_t data_size);

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

static int chunk_handler_dqt(jpg_file_params_t *jpg_param, void* data, uint16_t data_size);
static int chunk_handler_sof0(jpg_file_params_t *jpg_param, void* data, uint16_t data_size);
static int chunk_handler_dht(jpg_file_params_t *jpg_param, void* data, uint16_t data_size);
static int chunk_handler_sos(jpg_file_params_t *jpg_param, void* data, uint16_t data_size);
static int chunk_handler_comment(jpg_file_params_t *jpg_param, void* data, uint16_t data_size);

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
static bool log_enabled = true;
static void log_str(const char* format_str, ...)
{
    if (!log_enabled)
        return;
    char buf_str[4096] = {0};
    va_list ap;

    va_start(ap, format_str);
    vsprintf(buf_str, format_str, ap);
    va_end(ap);

    printf("%s", buf_str);
    fflush(stdout);
    FILE* log_file = fopen("./log.log", "a");
    if (!log_file){
        printf("Can't open log file.");
        return;
    }
    fputs(buf_str, log_file);
    
    fclose(log_file);
}

int jpeg_codec_zigzag_to_matrix(int8_t ***matrix_ptr, int8_t * l_array, size_t arr_size)
{
    int8_t **b_matrix = NULL;

    b_matrix = calloc(8, sizeof(int8_t*));
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
    log_str("=== Dump of jpg_file ===\n");
    log_str("[DQT] Number of DQT tables: %d\n", jpg_param->dqt_tables_cnt);
    for (int i = 0; i < jpg_param->dqt_tables_cnt; i++){
        int8_t **dqt_table = NULL;
        log_str("[DQT] \tDQT table id: %d\n", jpg_param->dqt_param[i]->header.tbl_id);
        log_str("[DQT] \tDQT table value size: %d byte\n", jpg_param->dqt_param[i]->header.tbl_value_size);
        jpeg_codec_zigzag_to_matrix(&dqt_table, jpg_param->dqt_param[i]->table, 64);
        log_str("[DQT] \tDQT table:\n");
        for (int k = 0; k < 8; k++){
            log_str("[DQT] \t\t[");
            for (int j = 0; j < 8; j++){
                log_str("%hx ", (uint8_t)dqt_table[k][j]);
            }
            log_str("]\n");
        }
        for (int i = 0; i < 8;i++)
            free(dqt_table[i]);
        
        free(dqt_table);
    }
    log_str("\n");
    log_str("[SOF0] Precision: %d\n", jpg_param->sof0->header.precision);
    log_str("[SOF0] Image height: %d\n", jpg_param->sof0->header.height);
    log_str("[SOF0] Image width: %d\n", jpg_param->sof0->header.width);
    log_str("[SOF0] Number of channels: %d\n", jpg_param->sof0->header.channel_cnt);
    for (int i = 0; i < jpg_param->sof0->header.channel_cnt; i++){
        log_str("[SOF0] \tChannel id: %d\n", jpg_param->sof0->channels[i].id);
        log_str("[SOF0] \tHorizontal thinning: %d\n", jpg_param->sof0->channels[i].h_thinning);
        log_str("[SOF0] \tVertical thinning: %d\n", jpg_param->sof0->channels[i].v_thinning);
        log_str("[SOF0] \tDQT table id: %d\n\n", jpg_param->sof0->channels[i].dqt_id);
    }

    log_str("\n");
    log_str("[DHT] DHTs count: %d\n", jpg_param->dht_cnt);
    for (int i = 0; i < jpg_param->dht_cnt; i++){
        log_str("[DHT] \tTable id: %d\n", jpg_param->dht[i]->header.table_id);
        log_str("[DHT] \tCLASS: %s\n", jpg_param->dht[i]->header.table_class ? "AC" : "DC");
        log_str("[DHT] \tCodes length counts:\n");
        log_str("[DHT] \t");
        for (int j = 0; j < 16; j++){
            log_str("[%d] ", jpg_param->dht[i]->header.codes_cnts_by_length[j]);
        }
        log_str("\n");
        log_str("[DHT] \tCodes: \n");
        log_str("[DHT] \t");
        for (int k = 0; k < jpg_param->dht[i]->header.codes_value_cnt; k++){
            if (k%16 == 0)
                log_str("\n[DHT] \t");
            log_str("[0x%.2X] ", jpg_param->dht[i]->codes_value[k]);
        }
        log_str("\n\n");
    }

    log_str("\n");
    log_str("[SOS] Channels count: %d\n", jpg_param->sos->channel_cnt);
    for (int i = 0; i < jpg_param->sos->channel_cnt; i++){
        log_str("[SOS] \tChannel id: %d\n", jpg_param->sos->channels[i].channel_id);
        log_str("[SOS] \tHuffman DC table id: %d\n", jpg_param->sos->channels[i].huffman_table_dc_id);
        log_str("[SOS] \tHuffman AC table id: %d\n", jpg_param->sos->channels[i].huffman_table_ac_id);
        log_str("\n");
    }

}

int jpg_codec_jpg_param_remove(jpg_file_params_t *jpg_param)
{
    for (int i = 0 ; i < jpg_param->dqt_tables_cnt; i++){
        if (jpg_param->dqt_param[i]){
            free(jpg_param->dqt_param[i]);
        }
    }
    if (jpg_param->dqt_param)
        free(jpg_param->dqt_param);

    if (jpg_param->sof0)
        free(jpg_param->sof0);

    for (int i = 0 ; i < jpg_param->dht_cnt; i++){
        if (jpg_param->dht[i])
            free(jpg_param->dht[i]);
    }
    if (jpg_param->dht)
        free(jpg_param->dht);

    if (jpg_param->sos)
        free(jpg_param->sos);

    if (jpg_param->encoded_data)
        free(jpg_param->encoded_data);

    return 0;
}

static int8_t **s_get_dqt(jpg_decoding_params_t *decode_param, int channel_id)
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

int jpg_codec_file_decode(FILE *jpg_file, void **out_pixel_array)
{
    jpg_file_params_t jpg_param = {};

    if (!jpg_file || !out_pixel_array){
        return -1;
    }
    
    clock_t begin = clock();

    uint16_t l_begin_marker = getc(jpg_file) << 8;
    l_begin_marker |= getc(jpg_file);

    if (l_begin_marker != 0xFFD8){
        return -2;
    }
  
    uint16_t l_section_marker = 0;
    // parse jpg file
    for (;;){
        if(l_section_marker != CHUNK_TYPE_SOS){
            l_section_marker = getc(jpg_file) << 8;
            l_section_marker |= getc(jpg_file);

            if(l_section_marker == 0xFFD9 || feof(jpg_file)){
                jpg_codec_jpg_param_remove(&jpg_param);
                return -6;
            }

            uint16_t l_section_size = getc(jpg_file) << 8;
            l_section_size |= getc(jpg_file);

            if (!l_section_size)
                continue;

            l_section_size -= sizeof(l_section_size);
            uint8_t *l_data = calloc(l_section_size, sizeof(uint8_t));
            int l_size = fread (l_data, sizeof(char), l_section_size, jpg_file);

            for(int i = 0; i < sizeof(s_block_handlers)/sizeof(handlers_table_item_t); i++){
                if (s_block_handlers[i].type == l_section_marker && s_block_handlers[i].handler){
                    s_block_handlers[i].handler(&jpg_param, l_data, l_section_size);
                    break;
                }
            }

            if (l_data)
                free(l_data);
        } else  {
            // let's compute remain data size
            // save current position
            unsigned long position = ftell(jpg_file);
            // go to end
            fseek(jpg_file, 0, SEEK_END);
            // save end position
            unsigned long end_position = ftell(jpg_file);
            // go to old position
            fseek(jpg_file, position, SEEK_SET);

            size_t remain_size = end_position - position + 1;
            uint8_t* b_encoded_data = malloc(remain_size);

            int l_size = fread (b_encoded_data, sizeof(uint8_t), remain_size, jpg_file);
            for (int i = 0; i < remain_size - 1; i++){
                if (b_encoded_data[i+1] == 0xD9 && b_encoded_data[i] == 0xFF){
                    remain_size = i * sizeof(uint8_t);
                    b_encoded_data = realloc(b_encoded_data, remain_size);
                    jpg_param.encoded_data = b_encoded_data;
                    jpg_param.encoded_data_size = remain_size;
                    break;
                } 
            }

            if (!jpg_param.encoded_data){
                free(b_encoded_data);
                jpg_codec_jpg_param_remove(&jpg_param);
                return -7;
            } else 
                break;
            
        }
    }

    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    log_str("File decoded in %f sec\n", time_spent);
    clock_t begin_block = clock();

    jpg_codec_file_dump(&jpg_param);
    jpg_decoding_params_t jpg_decode_params = {};
    jpg_decode_params.dqt_tables_cnt = jpg_param.dqt_tables_cnt;
    jpg_decode_params.dqt_tables = calloc(jpg_param.dqt_tables_cnt, sizeof(uint16_t**));
    for (int i = 0; i < jpg_param.dqt_tables_cnt; i++){
        int8_t **dqt_table = NULL;
        jpeg_codec_zigzag_to_matrix(&dqt_table, jpg_param.dqt_param[i]->table, 64);
        jpg_decode_params.dqt_tables[i] = dqt_table;
    }

    jpg_decode_params.huffman_trees = calloc(jpg_param.dht_cnt, sizeof(huffman_tree_t*));
    jpg_decode_params.huffman_trees_cnt = jpg_param.dht_cnt;
    for(int i = 0; i < jpg_param.dht_cnt; i++){
        jpg_decode_params.huffman_trees[i] = huffman_tree_create(jpg_param.dht[i]->header.table_class, 
                                            jpg_param.dht[i]->header.table_id,
                                            jpg_param.dht[i]->header.codes_cnts_by_length, 
                                            jpg_param.dht[i]->codes_value, 
                                            jpg_param.dht[i]->header.codes_value_cnt);
        huffman_tree_dump(jpg_decode_params.huffman_trees[i]);
    }
    



    size_t sos_size = sizeof(uint8_t) + jpg_param.sos->channel_cnt * sizeof(jpg_sos_channel_t);
    jpg_decode_params.sos = calloc(1, sos_size);
    memcpy(jpg_decode_params.sos, jpg_param.sos, sos_size);

    size_t sof0_size = sizeof(jpg_param.sof0->header) + jpg_param.sof0->header.channel_cnt * sizeof(jpg_sof0_channel);
    jpg_decode_params.sof0 = calloc(1, sof0_size);
    memcpy(jpg_decode_params.sof0, jpg_param.sof0, sof0_size);

    jpg_decode_params.encoded_data = calloc(jpg_param.encoded_data_size, sizeof(uint8_t));
    jpg_decode_params.encoded_data_size = jpg_param.encoded_data_size;
    memcpy(jpg_decode_params.encoded_data, jpg_param.encoded_data, jpg_decode_params.encoded_data_size);
    
    log_str("bitflow size = %lu\n", jpg_decode_params.encoded_data_size);

    jpg_codec_jpg_param_remove(&jpg_param);

    // for (size_t i =0; i < jpg_decode_params.encoded_data_size; i++){
    //     char str[9] = {};
    //     print_binary_8bit(jpg_decode_params.encoded_data[i] , str);
    //     log_str("%s|", str);
    // }
    // log_str("\n");

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
    
    log_str("[*] Found %d DCT Matrices\n", matrix_cnt);
    for(int i = 0; i < matrix_cnt; i++){
        log_str("[*] DCT Matrix #%d\n", i);
        for (int k = 0; k < 8; k++){
            log_str("[*] \t\t[");
            for (int j = 0; j < 8; j++){
                log_str("%d ", matrix[i][k][j]);
            }
            log_str("]\n");
        }
    }

    end = clock();
    time_spent = (double)(end - begin_block) / CLOCKS_PER_SEC;
    log_str("All decode params compued in %f sec\n", time_spent);
    begin_block = clock();

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
        log_str("[*] Matrix #%d\n", i);
        for (int k = 0; k < 8; k++){
            log_str("[*] \t\t[");
            for (int j = 0; j < 8; j++){
                log_str("%d ", matrix[i][k][j]);
            }
            log_str("]\n");
        }
    }

    // Let's quantize matrices
    int current_channel_cnt = 0;
    channel_name_t current_channel_id = CHANNEL_Y;
    for (int i = 0; i < matrix_cnt; i++)
    {
        int8_t **dqt_table_b = (int8_t **)s_get_dqt(&jpg_decode_params, current_channel_id);

        for (int k = 0; k < 8; k++){
            for (int j = 0; j < 8; j++){
                matrix[i][k][j] *= (uint8_t)dqt_table_b[k][j];
            }
        }
        if (++current_channel_cnt >= b_channels_cnt[current_channel_id-1]){
            if(++current_channel_id > 3)
                current_channel_id = CHANNEL_Y;
        }
    }

    for(int i = 0; i < matrix_cnt; i++){
        log_str("[*] Quantize Matrix #%d\n", i);
        for (int k = 0; k < 8; k++){
            log_str("[*] \t\t[");
            for (int j = 0; j < 8; j++){
                log_str("%d ", matrix[i][k][j]);
            }
            log_str("]\n");
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
        log_str("[*] YCbCr Matrix #%d\n", i);
        for (int k = 0; k < 8; k++){
            log_str("[*] \t\t[");
            for (int j = 0; j < 8; j++){
                log_str("%d ", output_matrix[i][k][j]);
            }
            log_str("]\n");
        }
    }

    end = clock();
    time_spent = (double)(end - begin_block) / CLOCKS_PER_SEC;
    log_str("DCT made in  %f sec\n", time_spent);
    begin_block = clock();

    log_str("[*] Lets transform YCrCb to RGB.\n");
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


    end = clock();
    time_spent = (double)(end - begin_block) / CLOCKS_PER_SEC;
    log_str("All RGB pixels filled in  %f sec\n", time_spent);
    begin_block = clock();
    log_str("[*] RGB Matrices\n");
    for (int k = 0; k < 16; k++){
        log_str("[*] \t\t[");
        for (int j = 0; j < 16; j++){
            log_str("%d ", RGB_matrix[k][j].R);
        }
        log_str("]\n");
    }
    log_str("\n");
    for (int k = 0; k < 16; k++){
        log_str("[*] \t\t[");
        for (int j = 0; j < 16; j++){
            log_str("%d ", RGB_matrix[k][j].G);
        }
        log_str("]\n");
    }
    log_str("\n");
    for (int k = 0; k < 16; k++){
        log_str("[*] \t\t[");
        for (int j = 0; j < 16; j++){
            log_str("%d ", RGB_matrix[k][j].B);
        }
        log_str("]\n");
    }


    end = clock();
    time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    log_str("File decoded in %f sec\n", time_spent);
    return bmp_file_create(RGB_matrix, jpg_decode_params.sof0->header.height, jpg_decode_params.sof0->header.width);
    
    // return 0;
}


static int chunk_handler_dqt(jpg_file_params_t *jpg_param, void* data, uint16_t data_size)
{
    if (!data_size || !data || !jpg_param)
        return -1;

   

    uint8_t *l_b_data = (uint8_t*)data;
    while (l_b_data < (uint8_t*)data + (size_t)data_size){
        jpg_dqt_t *l_buf_dqt = calloc(data_size - sizeof(uint8_t) + sizeof(l_buf_dqt->header), sizeof(uint8_t));
        uint8_t l_buf_params = *(uint8_t*)l_b_data;
        l_buf_dqt->header.tbl_value_size = (int)((l_buf_params & 0xF0) >> 4) + 1;
        l_buf_dqt->header.tbl_id = (int)(l_buf_params & 0x0F);
        l_b_data++;
        memcpy(l_buf_dqt->table, l_b_data, 64);
        l_b_data += 64;

        jpg_param->dqt_tables_cnt++;
        jpg_param->dqt_param = (jpg_dqt_t **)realloc(jpg_param->dqt_param, jpg_param->dqt_tables_cnt * sizeof(jpg_dqt_t *));
        jpg_param->dqt_param[jpg_param->dqt_tables_cnt - 1] = l_buf_dqt;
    }
    

    return 0;
}


static int chunk_handler_sof0(jpg_file_params_t *jpg_param, void* data, uint16_t data_size)
{
    if (!data_size || !data || !jpg_param)
        return -1;

    void* data_pointer = data;

    uint8_t precision = *(uint8_t*)data_pointer;
    uint16_t height = ((*(uint8_t*)(++data_pointer)) << 8 | (*(uint8_t*)(++data_pointer)));
    uint16_t width = ((*(uint8_t*)(++data_pointer)) << 8 | (*(uint8_t*)(++data_pointer)));
    uint8_t chanel_cnt = *(uint8_t*)(++data_pointer);

    jpg_sof0_t *l_buf_sof = calloc(sizeof(l_buf_sof->header) + chanel_cnt*sizeof(jpg_sof0_channel), sizeof(uint8_t));
    l_buf_sof->header.precision = precision;
    l_buf_sof->header.height = height;
    l_buf_sof->header.width = width;
    l_buf_sof->header.channel_cnt = chanel_cnt;

    for (int i = 0; i < chanel_cnt; i++){
        l_buf_sof->channels[i].id = *(uint8_t*)++data_pointer;
        l_buf_sof->channels[i].h_thinning = ((*(uint8_t*)++data_pointer & 0xF0) >> 4);
        l_buf_sof->channels[i].v_thinning = *(uint8_t*)data_pointer & 0x0F;
        l_buf_sof->channels[i].dqt_id = (*(uint8_t*)++data_pointer);
    }

    jpg_param->sof0 = l_buf_sof;

    return 0;
}

static int chunk_handler_dht(jpg_file_params_t *jpg_param, void* data, uint16_t data_size)
{
    if (!data_size || !data || !jpg_param)
        return -1;

    uint8_t* data_pointer = (uint8_t*)data;

    while(data_pointer < (uint8_t*)data + (size_t)data_size){
        jpg_dht_t *l_buf_dht = calloc(1, sizeof(jpg_dht_t));

        l_buf_dht->header.table_class = (*(uint8_t*)data_pointer & 0xF0) >> 4;
        l_buf_dht->header.table_id = *(uint8_t*)data_pointer & 0x0F;
        data_pointer++;
        memcpy(l_buf_dht->header.codes_cnts_by_length, data_pointer, 16);
        data_pointer += 16;

        for(int i = 0; i < 16; i++){
            l_buf_dht->header.codes_value_cnt += l_buf_dht->header.codes_cnts_by_length[i];
        }

        l_buf_dht = (jpg_dht_t*)realloc(l_buf_dht, sizeof(l_buf_dht->header) + l_buf_dht->header.codes_value_cnt * sizeof(uint8_t));
        memcpy(l_buf_dht->codes_value, data_pointer, l_buf_dht->header.codes_value_cnt);
        data_pointer += l_buf_dht->header.codes_value_cnt;

        jpg_param->dht_cnt++;
        jpg_param->dht = (jpg_dht_t **)realloc(jpg_param->dht, jpg_param->dht_cnt * sizeof(jpg_dht_t *));
        jpg_param->dht[jpg_param->dht_cnt - 1] = l_buf_dht;
    }
    return 0;
}

static int chunk_handler_sos(jpg_file_params_t *jpg_param, void* data, uint16_t data_size)
{
    if (!data_size || !data || !jpg_param)
        return -1;

    void* data_pointer = data;

    uint8_t l_channels_cnt = *(uint8_t*)data_pointer;
    jpg_sos_t *l_buf_sos = calloc(1, sizeof(l_buf_sos->channel_cnt) + l_channels_cnt*sizeof(jpg_sos_channel_t));
    l_buf_sos->channel_cnt = l_channels_cnt;
    data_pointer++;
    for(int i = 0; i < l_channels_cnt; i++){
        l_buf_sos->channels[i].channel_id = *(uint8_t*)data_pointer;
        data_pointer++;
        l_buf_sos->channels[i].huffman_table_dc_id = (*(uint8_t*)data_pointer & 0xF0) >> 4;
        l_buf_sos->channels[i].huffman_table_ac_id = *(uint8_t*)data_pointer & 0x0F;
        data_pointer++;
    }

    jpg_param->sos = l_buf_sos;
    return 0;
}

static int chunk_handler_comment(jpg_file_params_t *jpg_param, void* data, uint16_t data_size)
{
    if (!data_size || !data || !jpg_param)
        return -1;

    char *l_comment = calloc(data_size + 1, sizeof(char));
    memcpy(l_comment, data, data_size);

    log_str("Comment: %s\n", (char*)l_comment);
    free(l_comment);

    return 0;
}

static huffman_tree_t *s_get_dht(huffman_tree_t** huffman_trees, int huffman_trees_cnt, coeff_name_t current_coeff, uint8_t tree_id)
{
    // log_str("Search table with id = %d and tree class = %d\n", tree_id, current_coeff);
    for (int i = 0; i < huffman_trees_cnt; i++){
        if ((huffman_trees[i]->id == tree_id) && (huffman_trees[i]->tree_class == (uint8_t)current_coeff)){
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

    huffman_tree_t *huffman_tree_current = s_get_dht(decoding_param->huffman_trees, decoding_param->huffman_trees_cnt, 
                                                     current_coeff, decoding_param->sos->channels[(uint8_t)current_channel-1].huffman_table_dc_id);
    tree_node_t *current_node = huffman_tree_current->tree;
    uint8_t current_value = 0;

    struct jpg_bitflow_t* bf = jpg_bitflow_allocate(decoding_param->encoded_data, decoding_param->encoded_data_size);
    if (!bf){
        log_str("Memory allocation error\n");
        return -1;
    }

    for (;;){
        if (current_node && !current_node->leaf_node){
            int curr_bit = false;
            ret_code = jpg_bitflow_get_next_bit(bf, &curr_bit);
            if (ret_code == JPG_BITFLOW_END_OF_FLOW)
                break;

            if (ret_code != JPG_BITFLOW_RET_OK)
                goto ret_error;

            current_node = curr_bit ? (current_node->right) : (current_node->left);
            if (!current_node){
                ret_code = -100;
                goto ret_error;
            }
            continue;
        } 

        if(current_node->value == 0){
            if (current_coeff == COEFF_NAME_DC){
                l_zigzag_matrixes[current_l_zigzag_matrix_cnt][b_zigzag_pos] = 0;
                // log_str("dc coeff_value = 0\n");
                b_zigzag_pos++;
                current_coeff = COEFF_NAME_AC;
                uint8_t tree_id = decoding_param->sos->channels[(uint8_t)current_channel-1].huffman_table_ac_id;
                huffman_tree_current = s_get_dht(decoding_param->huffman_trees, decoding_param->huffman_trees_cnt, 
                                                    current_coeff, tree_id);
                current_node = huffman_tree_current->tree;   
            }  else {
                // log_str("set next ac coeff_value = 0\n");
                current_l_zigzag_matrix_cnt++;
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
                // log_str("Set channel = %d\n", current_channel);
                uint8_t tree_id = decoding_param->sos->channels[(uint8_t)current_channel-1].huffman_table_dc_id;
                huffman_tree_current = s_get_dht(decoding_param->huffman_trees, decoding_param->huffman_trees_cnt, 
                                                    current_coeff, tree_id);
                current_node = huffman_tree_current->tree; 
            }                                
        } else {
            if (current_coeff == COEFF_NAME_DC){
                int64_t coeff_value = 0;
                if((ret_code = jpg_bitflow_get_next_i64_bits(bf, &coeff_value, current_node->value)) != JPG_BITFLOW_RET_OK)
                    goto ret_error;
                // log_str("dc coeff_value = %d\n", coeff_value);
                l_zigzag_matrixes[current_l_zigzag_matrix_cnt][b_zigzag_pos] = (coeff_value & (1 << (current_node->value-1))) ? coeff_value : coeff_value - s_pow_2(current_node->value) + 1;
                b_zigzag_pos++;
                current_coeff = COEFF_NAME_AC;
                uint8_t tree_id = decoding_param->sos->channels[(uint8_t)current_channel-1].huffman_table_ac_id;
                huffman_tree_current = s_get_dht(decoding_param->huffman_trees, decoding_param->huffman_trees_cnt, 
                                                    current_coeff, tree_id);
                current_node = huffman_tree_current->tree; 
            } else {
                int64_t coeff_value = 0;
                uint8_t zero_cnt = (current_node->value & 0xF0) >> 4;
                uint16_t coef_lng = (current_node->value & 0x0F);
                b_zigzag_pos += zero_cnt;
                // if (zero_cnt) log_str("skip %d ac coeff remain zero.\n", zero_cnt);
                if (b_zigzag_pos <= 63){
                    if((ret_code = jpg_bitflow_get_next_i64_bits(bf, &coeff_value, coef_lng)) != JPG_BITFLOW_RET_OK)
                        goto ret_error;
                    
                    // log_str("ac coeff_value = %d, coef_lng = %u\n", coeff_value, (unsigned short)coef_lng);
                    // if (coeff_value){
                        l_zigzag_matrixes[current_l_zigzag_matrix_cnt][b_zigzag_pos] = coeff_value ? (coeff_value & (1 << (coef_lng-1))) ? coeff_value : coeff_value + 1 - s_pow_2(coef_lng) : 0;
                        // log_str("ac coeff_value after transforming = %d\n", l_zigzag_matrixes[current_l_zigzag_matrix_cnt][b_zigzag_pos]);
                        b_zigzag_pos++;
                    // }
                }
                if (b_zigzag_pos > 63){
                    // log_str("end of matrix\n");
                    current_l_zigzag_matrix_cnt++;
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
                    uint8_t tree_id = decoding_param->sos->channels[(uint8_t)current_channel-1].huffman_table_dc_id;
                    huffman_tree_current = s_get_dht(decoding_param->huffman_trees, decoding_param->huffman_trees_cnt, 
                                                        current_coeff, tree_id);              
                } 
                current_node = huffman_tree_current->tree;
            }
        }
    }

    if (zigzag_matrix)
        *zigzag_matrix = l_zigzag_matrixes;

    return current_l_zigzag_matrix_cnt + 1;

ret_error:
    jpg_bitflow_deallocate(bf);
    return ret_code;
}