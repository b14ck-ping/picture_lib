#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jpg_bitflow.h"

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

static int expect_logical_bytes(const uint8_t *raw,
                                size_t raw_len,
                                const uint8_t *expected,
                                size_t expected_len)
{
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate((uint8_t *)raw, raw_len);
    CHECK(bitflow != NULL);

    for (size_t byte = 0; byte < expected_len; ++byte) {
        for (size_t bit = 0; bit < 8; ++bit) {
            int actual = -1;
            int wanted = (expected[byte] >> (7U - bit)) & 1U;

            CHECK(jpg_bitflow_get_next_bit(bitflow, &actual) == JPG_BITFLOW_RET_OK);
            CHECK(actual == wanted);
        }
    }

    int bit = -1;
    CHECK(jpg_bitflow_get_next_bit(bitflow, &bit) == JPG_BITFLOW_END_OF_FLOW);
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_single_byte_msb_first(void)
{
    const uint8_t raw[] = {0xA5};
    const uint8_t expected[] = {0xA5};

    return expect_logical_bytes(raw, sizeof(raw), expected, sizeof(expected));
}

static int test_crosses_byte_boundary(void)
{
    const uint8_t raw[] = {0xA5, 0x5A};
    const uint8_t expected[] = {0xA5, 0x5A};

    return expect_logical_bytes(raw, sizeof(raw), expected, sizeof(expected));
}

static int test_stuffed_ff_in_middle(void)
{
    const uint8_t raw[] = {0xA5, 0xFF, 0x00, 0x5A};
    const uint8_t expected[] = {0xA5, 0xFF, 0x5A};

    return expect_logical_bytes(raw, sizeof(raw), expected, sizeof(expected));
}

static int test_stuffed_ff_at_end(void)
{
    const uint8_t raw[] = {0xA5, 0xFF, 0x00};
    const uint8_t expected[] = {0xA5, 0xFF};

    return expect_logical_bytes(raw, sizeof(raw), expected, sizeof(expected));
}

static int test_only_stuffed_ff(void)
{
    const uint8_t raw[] = {0xFF, 0x00};
    const uint8_t expected[] = {0xFF};

    return expect_logical_bytes(raw, sizeof(raw), expected, sizeof(expected));
}

static int test_get_exactly_eight_bits(void)
{
    uint8_t raw[] = {0xA5};
    uint8_t output[] = {0xFF};
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_n_bits(bitflow, output, 8) == JPG_BITFLOW_RET_OK);
    CHECK(output[0] == 0xA5);

    int bit = -1;
    CHECK(jpg_bitflow_get_next_bit(bitflow, &bit) == JPG_BITFLOW_END_OF_FLOW);
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_bulk_read_rejects_too_many_bits(void)
{
    uint8_t raw[] = {0xA5};
    uint8_t output[] = {0x00, 0x00};
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_n_bits(bitflow, output, 9) == JPG_BITFLOW_RANGE);

    int bit = -1;
    CHECK(jpg_bitflow_get_next_bit(bitflow, &bit) == JPG_BITFLOW_RET_OK);
    CHECK(bit == 1);
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_get_multiple_bytes(void)
{
    uint8_t raw[] = {0xA5, 0x5A};
    uint8_t output[] = {0x00, 0x00};
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_n_bits(bitflow, output, 16) == JPG_BITFLOW_RET_OK);
    CHECK(memcmp(output, raw, sizeof(raw)) == 0);

    int bit = -1;
    CHECK(jpg_bitflow_get_next_bit(bitflow, &bit) == JPG_BITFLOW_END_OF_FLOW);
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_get_non_byte_aligned_bits(void)
{
    uint8_t raw[] = {0xD6, 0x80};
    uint8_t output[] = {0x00, 0x00};
    const uint8_t expected[] = {0xD6, 0x80};
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_n_bits(bitflow, output, 10) == JPG_BITFLOW_RET_OK);
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_consecutive_bulk_reads_continue_from_cursor(void)
{
    uint8_t raw[] = {0xD6};
    uint8_t first = 0x00;
    uint8_t second = 0x00;
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_n_bits(bitflow, &first, 4) == JPG_BITFLOW_RET_OK);
    CHECK(jpg_bitflow_get_next_n_bits(bitflow, &second, 4) == JPG_BITFLOW_RET_OK);
    CHECK(first == 0xD0);
    CHECK(second == 0x60);
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_i64_reads_numeric_value(void)
{
    uint8_t raw[] = {0xB4}; /* First six bits: 101101 = 45. */
    int64_t value = 0;
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_i64_bits(bitflow, &value, 6) == JPG_BITFLOW_RET_OK);
    CHECK(value == INT64_C(45));
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_i64_crosses_unaligned_byte_boundary(void)
{
    uint8_t raw[] = {0xD6, 0x6B};
    int discarded_bit = 0;
    int64_t value = 0;
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    for (int i = 0; i < 3; ++i)
        CHECK(jpg_bitflow_get_next_bit(bitflow, &discarded_bit) == JPG_BITFLOW_RET_OK);

    /* After 110, the next nine bits are 101100110 = 358. */
    CHECK(jpg_bitflow_get_next_i64_bits(bitflow, &value, 9) == JPG_BITFLOW_RET_OK);
    CHECK(value == INT64_C(358));
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_i64_unstuffs_ff_00(void)
{
    uint8_t raw[] = {0xA5, 0xFF, 0x00, 0x5A};
    int64_t value = 0;
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_i64_bits(bitflow, &value, 24) == JPG_BITFLOW_RET_OK);
    CHECK(value == INT64_C(0xA5FF5A));
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_i64_supports_full_width(void)
{
    uint8_t raw[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    int64_t value = 0;
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_i64_bits(bitflow, &value, 64) == JPG_BITFLOW_RET_OK);
    CHECK(value == INT64_C(0x0123456789ABCDEF));
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_i64_overwrites_destination(void)
{
    uint8_t raw[] = {0x00};
    int64_t value = INT64_MAX;
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_i64_bits(bitflow, &value, 4) == JPG_BITFLOW_RET_OK);
    CHECK(value == 0);
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_i64_consecutive_reads_continue_from_cursor(void)
{
    uint8_t raw[] = {0xB6}; /* 101 | 10110 */
    int64_t first = 0;
    int64_t second = 0;
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_i64_bits(bitflow, &first, 3) == JPG_BITFLOW_RET_OK);
    CHECK(jpg_bitflow_get_next_i64_bits(bitflow, &second, 5) == JPG_BITFLOW_RET_OK);
    CHECK(first == INT64_C(5));
    CHECK(second == INT64_C(22));
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_i64_rejects_invalid_arguments(void)
{
    uint8_t raw[] = {0x80};
    int64_t value = 0;
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    CHECK(jpg_bitflow_get_next_i64_bits(NULL, &value, 1) == JPG_BITFLOW_BAD_ARG);
    CHECK(jpg_bitflow_get_next_i64_bits(bitflow, NULL, 1) == JPG_BITFLOW_BAD_ARG);
    CHECK(jpg_bitflow_get_next_i64_bits(bitflow, &value, 0) == JPG_BITFLOW_BAD_ARG);
    CHECK(jpg_bitflow_get_next_i64_bits(bitflow, &value, 65) == JPG_BITFLOW_RANGE);

    int bit = 0;
    CHECK(jpg_bitflow_get_next_bit(bitflow, &bit) == JPG_BITFLOW_RET_OK);
    CHECK(bit == 1);
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_allocate_copies_input(void)
{
    uint8_t raw[] = {0x80};
    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);

    raw[0] = 0x00;

    int bit = -1;
    CHECK(jpg_bitflow_get_next_bit(bitflow, &bit) == JPG_BITFLOW_RET_OK);
    CHECK(bit == 1);
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

static int test_invalid_arguments(void)
{
    uint8_t raw[] = {0x00};
    uint8_t output = 0x00;
    int bit = -1;

    CHECK(jpg_bitflow_allocate(NULL, sizeof(raw)) == NULL);
    CHECK(jpg_bitflow_allocate(raw, 0) == NULL);
    CHECK(jpg_bitflow_deallocate(NULL) == JPG_BITFLOW_BAD_ARG);
    CHECK(jpg_bitflow_get_next_bit(NULL, &bit) == JPG_BITFLOW_BAD_ARG);

    struct jpg_bitflow_t *bitflow = jpg_bitflow_allocate(raw, sizeof(raw));
    CHECK(bitflow != NULL);
    CHECK(jpg_bitflow_get_next_bit(bitflow, NULL) == JPG_BITFLOW_BAD_ARG);
    CHECK(jpg_bitflow_get_next_n_bits(NULL, &output, 1) == JPG_BITFLOW_BAD_ARG);
    CHECK(jpg_bitflow_get_next_n_bits(bitflow, NULL, 1) == JPG_BITFLOW_BAD_ARG);
    CHECK(jpg_bitflow_get_next_n_bits(bitflow, &output, 0) == JPG_BITFLOW_BAD_ARG);
    CHECK(jpg_bitflow_deallocate(bitflow) == JPG_BITFLOW_RET_OK);
    return 1;
}

int main(void)
{
    const test_case_t tests[] = {
        {"single byte is read MSB-first", test_single_byte_msb_first},
        {"reading crosses a byte boundary", test_crosses_byte_boundary},
        {"FF 00 is unstuffed in the middle", test_stuffed_ff_in_middle},
        {"FF 00 is unstuffed at the end", test_stuffed_ff_at_end},
        {"a stream containing only FF 00", test_only_stuffed_ff},
        {"bulk read returns exactly eight bits", test_get_exactly_eight_bits},
        {"bulk read rejects a request past the end", test_bulk_read_rejects_too_many_bits},
        {"bulk read returns multiple bytes", test_get_multiple_bytes},
        {"bulk read supports a partial final byte", test_get_non_byte_aligned_bits},
        {"consecutive bulk reads continue at the cursor", test_consecutive_bulk_reads_continue_from_cursor},
        {"i64 read returns a numeric value", test_i64_reads_numeric_value},
        {"i64 read crosses an unaligned byte boundary", test_i64_crosses_unaligned_byte_boundary},
        {"i64 read handles FF 00 stuffing", test_i64_unstuffs_ff_00},
        {"i64 read supports all 64 bits", test_i64_supports_full_width},
        {"i64 read overwrites its destination", test_i64_overwrites_destination},
        {"consecutive i64 reads continue at the cursor", test_i64_consecutive_reads_continue_from_cursor},
        {"i64 read rejects invalid arguments", test_i64_rejects_invalid_arguments},
        {"allocation copies its input", test_allocate_copies_input},
        {"invalid arguments are rejected", test_invalid_arguments},
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
        fprintf(stderr, "%zu of %zu tests failed\n", failed, test_count);
        return 1;
    }

    printf("All %zu tests passed\n", test_count);
    return 0;
}
