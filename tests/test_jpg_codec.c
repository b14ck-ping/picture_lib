#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jpg_codec.h"

typedef int (*test_fn_t)(void);

typedef struct {
    const char *name;
    test_fn_t function;
} test_case_t;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                      \
            fprintf(stderr, "  %s:%d: CHECK failed: %s\n",                    \
                    __FILE__, __LINE__, #condition);                             \
            return 0;                                                           \
        }                                                                       \
    } while (0)

#define GENERATED_BMP_PATH "../test_pic/test.bmp"

/*
 * 16x16 baseline JPEG with 4:2:0 sampling. The custom Huffman tables are:
 *
 *   DC:  0    -> category 0
 *   AC:  00   -> EOB
 *        01   -> ZRL (0xF0)
 *        10   -> run 14, category 1 (0xE1)
 *        11   -> run 0, category 10 (0x0A)
 *
 * Entropy bytes are appended by the individual tests.
 */
static const uint8_t jpeg_prefix[] = {
    0xFF, 0xD8,

    0xFF, 0xDB, 0x00, 0x43, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,

    0xFF, 0xC0, 0x00, 0x11,
    0x08, 0x00, 0x10, 0x00, 0x10, 0x03,
    0x01, 0x22, 0x00,
    0x02, 0x11, 0x00,
    0x03, 0x11, 0x00,

    0xFF, 0xC4, 0x00, 0x29,
    0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
    0x10,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xF0, 0xE1, 0x0A,

    0xFF, 0xDA, 0x00, 0x0C,
    0x03,
    0x01, 0x00,
    0x02, 0x00,
    0x03, 0x00,
    0x00, 0x3F, 0x00,
};

typedef struct {
    uint8_t bytes[16];
    size_t bit_count;
} bit_writer_t;

static void append_bits(bit_writer_t *writer, uint16_t value, uint8_t count)
{
    for (uint8_t i = 0; i < count; ++i) {
        size_t bit = writer->bit_count++;
        uint8_t source_shift = (uint8_t)(count - i - 1);
        uint8_t destination_shift = (uint8_t)(7 - bit % 8);

        if ((value >> source_shift) & 1U)
            writer->bytes[bit / 8] |= (uint8_t)(1U << destination_shift);
    }
}

static void finish_entropy_byte(bit_writer_t *writer)
{
    while (writer->bit_count % 8 != 0)
        append_bits(writer, 1, 1);
}

static void append_zero_block(bit_writer_t *writer)
{
    append_bits(writer, 0, 1); /* DC category 0. */
    append_bits(writer, 0, 2); /* AC EOB. */
}

static size_t build_jpeg(uint8_t *output, size_t capacity, const bit_writer_t *entropy)
{
    size_t output_size = 0;
    size_t entropy_size = entropy->bit_count / 8;

    if (capacity < sizeof(jpeg_prefix) + entropy_size * 2 + 2)
        return 0;

    memcpy(output, jpeg_prefix, sizeof(jpeg_prefix));
    output_size = sizeof(jpeg_prefix);

    for (size_t i = 0; i < entropy_size; ++i) {
        output[output_size++] = entropy->bytes[i];
        if (entropy->bytes[i] == 0xFF)
            output[output_size++] = 0x00;
    }

    output[output_size++] = 0xFF;
    output[output_size++] = 0xD9;
    return output_size;
}

static int decode_entropy(const bit_writer_t *entropy)
{
    uint8_t jpeg[256] = {0};
    void *pixels = NULL;
    size_t jpeg_size = build_jpeg(jpeg, sizeof(jpeg), entropy);
    jpg_codec_ret_code_t result;
    CHECK(jpeg_size != 0);

    remove(GENERATED_BMP_PATH);
    result = jpg_codec_memory_decode(jpeg, jpeg_size, &pixels);
    if (result != JPG_CODEC_RET_OK)
        fprintf(stderr, "  decoder returned %d\n", result);
    CHECK(result == JPG_CODEC_RET_OK);
    return 1;
}

static int test_zrl_can_finish_block_at_coefficient_64(void)
{
    bit_writer_t entropy = {0};

    append_bits(&entropy, 0, 1); /* DC category 0. */
    append_bits(&entropy, 1, 2); /* ZRL: position 1 -> 17. */
    append_bits(&entropy, 1, 2); /* ZRL: position 17 -> 33. */
    append_bits(&entropy, 2, 2); /* 0xE1: position 33 -> 47. */
    append_bits(&entropy, 1, 1); /* Positive category-1 coefficient at 47. */
    append_bits(&entropy, 1, 2); /* ZRL: position 48 -> 64. */

    for (int block = 1; block < 6; ++block)
        append_zero_block(&entropy);

    finish_entropy_byte(&entropy);
    CHECK(decode_entropy(&entropy));
    remove(GENERATED_BMP_PATH);
    return 1;
}

static int test_zero_amplitude_bits_produce_negative_coefficient(void)
{
    bit_writer_t entropy = {0};

    append_bits(&entropy, 0, 1);  /* DC category 0. */
    append_bits(&entropy, 3, 2);  /* AC 0x0A. */
    append_bits(&entropy, 0, 10); /* Raw zero extends to -1023, not zero. */
    append_bits(&entropy, 0, 2);  /* EOB. */

    for (int block = 1; block < 6; ++block)
        append_zero_block(&entropy);

    finish_entropy_byte(&entropy);
    CHECK(decode_entropy(&entropy));

    FILE *bmp = fopen(GENERATED_BMP_PATH, "rb");
    CHECK(bmp != NULL);
    CHECK(fseek(bmp, 54, SEEK_SET) == 0);

    bool found_non_neutral_pixel = false;
    for (size_t i = 0; i < 16U * 16U * 3U; ++i) {
        uint8_t channel = 0;
        CHECK(fread(&channel, sizeof(channel), 1, bmp) == 1);
        if (channel != 128)
            found_non_neutral_pixel = true;
    }

    fclose(bmp);
    remove(GENERATED_BMP_PATH);
    CHECK(found_non_neutral_pixel);
    return 1;
}

int main(void)
{
    const test_case_t tests[] = {
        {"ZRL can fill the final sixteen AC positions", test_zrl_can_finish_block_at_coefficient_64},
        {"zero amplitude bits extend to a negative coefficient", test_zero_amplitude_bits_produce_negative_coefficient},
    };

    size_t failed = 0;
    const size_t test_count = sizeof(tests) / sizeof(tests[0]);

    for (size_t i = 0; i < test_count; ++i) {
        if (tests[i].function()) {
            printf("PASS: %s\n", tests[i].name);
        } else {
            fprintf(stderr, "FAIL: %s\n", tests[i].name);
            ++failed;
        }
    }

    if (failed != 0) {
        fprintf(stderr, "%zu of %zu jpg codec tests failed\n", failed, test_count);
        return 1;
    }

    printf("All %zu jpg codec tests passed\n", test_count);
    return 0;
}
