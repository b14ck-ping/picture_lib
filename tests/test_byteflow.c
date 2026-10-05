#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jpg_byteflow.h"

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

static int test_reads_bytes_in_order(void)
{
    const uint8_t data[] = {0x12, 0xAB, 0x00};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    for (size_t i = 0; i < sizeof(data); ++i) {
        uint8_t value = 0xFF;
        CHECK(jpg_byteflow_get_next_byte(reader, &value) == JPG_BYTEFLOW_RET_OK);
        CHECK(value == data[i]);
    }

    uint8_t value = 0x5A;
    CHECK(jpg_byteflow_get_next_byte(reader, &value) == JPG_BYTEFLOW_END_OF_FLOW);
    CHECK(value == 0x5A);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_reads_u16_big_endian(void)
{
    const uint8_t data[] = {0xFF, 0xD8, 0x12};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    uint16_t marker = 0;
    CHECK(jpg_byteflow_get_next_bytes_u16(reader, &marker) == JPG_BYTEFLOW_RET_OK);
    CHECK(marker == UINT16_C(0xFFD8));

    uint8_t trailing = 0;
    CHECK(jpg_byteflow_get_next_byte(reader, &trailing) == JPG_BYTEFLOW_RET_OK);
    CHECK(trailing == 0x12);
    CHECK(jpg_byteflow_get_next_byte(reader, &trailing) == JPG_BYTEFLOW_END_OF_FLOW);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_u16_range_error_does_not_advance_cursor(void)
{
    const uint8_t data[] = {0xA5};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    uint16_t value = UINT16_C(0xBEEF);
    CHECK(jpg_byteflow_get_next_bytes_u16(reader, &value) == JPG_BYTEFLOW_RANGE);
    CHECK(value == UINT16_C(0xBEEF));

    uint8_t byte = 0;
    CHECK(jpg_byteflow_get_next_byte(reader, &byte) == JPG_BYTEFLOW_RET_OK);
    CHECK(byte == 0xA5);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_reads_multiple_bytes(void)
{
    const uint8_t data[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    const uint8_t expected[] = {0x10, 0x20, 0x30};
    uint8_t output[sizeof(expected)] = {0};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    CHECK(jpg_byteflow_get_next_n_bytes(reader, output, sizeof(output)) == JPG_BYTEFLOW_RET_OK);
    CHECK(memcmp(output, expected, sizeof(expected)) == 0);

    uint16_t tail = 0;
    CHECK(jpg_byteflow_get_next_bytes_u16(reader, &tail) == JPG_BYTEFLOW_RET_OK);
    CHECK(tail == UINT16_C(0x4050));
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_bulk_range_error_does_not_modify_state(void)
{
    const uint8_t data[] = {0x11, 0x22};
    uint8_t output[] = {0xAA, 0xBB, 0xCC};
    const uint8_t unchanged[] = {0xAA, 0xBB, 0xCC};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    CHECK(jpg_byteflow_get_next_n_bytes(reader, output, sizeof(output)) == JPG_BYTEFLOW_RANGE);
    CHECK(memcmp(output, unchanged, sizeof(output)) == 0);

    uint8_t first = 0;
    CHECK(jpg_byteflow_get_next_byte(reader, &first) == JPG_BYTEFLOW_RET_OK);
    CHECK(first == 0x11);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_bulk_read_reports_end_of_flow(void)
{
    const uint8_t data[] = {0x7E};
    uint8_t output = 0;
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    CHECK(jpg_byteflow_get_next_byte(reader, &output) == JPG_BYTEFLOW_RET_OK);
    CHECK(jpg_byteflow_get_next_n_bytes(reader, &output, 1) == JPG_BYTEFLOW_END_OF_FLOW);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_operations_can_consume_remaining_bytes(void)
{
    const uint8_t data[] = {0x11, 0x22, 0x33};
    uint8_t output[sizeof(data)] = {0};
    const uint8_t *span = NULL;

    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);
    CHECK(jpg_byteflow_get_next_n_bytes(reader, output, sizeof(output)) == JPG_BYTEFLOW_RET_OK);
    CHECK(memcmp(output, data, sizeof(data)) == 0);
    CHECK(jpg_byteflow_get_next_byte(reader, &output[0]) == JPG_BYTEFLOW_END_OF_FLOW);
    jpg_byteflow_deallocate(reader);

    reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);
    CHECK(jpg_byteflow_get_next_n_bytes_span(reader, &span, sizeof(data)) == JPG_BYTEFLOW_RET_OK);
    CHECK(span == data);
    CHECK(jpg_byteflow_get_next_byte(reader, &output[0]) == JPG_BYTEFLOW_END_OF_FLOW);
    jpg_byteflow_deallocate(reader);

    reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);
    CHECK(jpg_byteflow_skip_next_n_bytes(reader, sizeof(data)) == JPG_BYTEFLOW_RET_OK);
    CHECK(jpg_byteflow_get_next_byte(reader, &output[0]) == JPG_BYTEFLOW_END_OF_FLOW);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_span_returns_data_and_advances_cursor(void)
{
    const uint8_t data[] = {0x10, 0x20, 0x30, 0x40};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    const uint8_t *span = NULL;
    CHECK(jpg_byteflow_get_next_n_bytes_span(reader, &span, 3) == JPG_BYTEFLOW_RET_OK);
    CHECK(span == data);
    CHECK(memcmp(span, data, 3) == 0);

    uint8_t trailing = 0;
    CHECK(jpg_byteflow_get_next_byte(reader, &trailing) == JPG_BYTEFLOW_RET_OK);
    CHECK(trailing == 0x40);
    CHECK(jpg_byteflow_get_next_byte(reader, &trailing) == JPG_BYTEFLOW_END_OF_FLOW);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_span_range_error_preserves_cursor_and_output(void)
{
    const uint8_t data[] = {0xA5, 0x5A};
    const uint8_t *span = (const uint8_t *)(uintptr_t)UINTPTR_MAX;
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    CHECK(jpg_byteflow_get_next_n_bytes_span(reader, &span, 3) == JPG_BYTEFLOW_RANGE);
    CHECK(span == (const uint8_t *)(uintptr_t)UINTPTR_MAX);

    uint8_t first = 0;
    CHECK(jpg_byteflow_get_next_byte(reader, &first) == JPG_BYTEFLOW_RET_OK);
    CHECK(first == 0xA5);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_subflow_is_bounded_and_advances_parent(void)
{
    const uint8_t data[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    uint8_t value = 0;
    struct byte_flow *subflow = NULL;
    CHECK(jpg_byteflow_get_next_byte(reader, &value) == JPG_BYTEFLOW_RET_OK);
    CHECK(value == 0x10);
    CHECK(jpg_byteflow_get_next_n_bytes_subflow(reader, &subflow, 2) == JPG_BYTEFLOW_RET_OK);
    CHECK(subflow != NULL);

    CHECK(jpg_byteflow_get_next_byte(subflow, &value) == JPG_BYTEFLOW_RET_OK);
    CHECK(value == 0x20);
    CHECK(jpg_byteflow_get_next_byte(subflow, &value) == JPG_BYTEFLOW_RET_OK);
    CHECK(value == 0x30);
    CHECK(jpg_byteflow_get_next_byte(subflow, &value) == JPG_BYTEFLOW_END_OF_FLOW);

    CHECK(jpg_byteflow_get_next_byte(reader, &value) == JPG_BYTEFLOW_RET_OK);
    CHECK(value == 0x40);
    jpg_byteflow_deallocate(subflow);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_subflow_error_preserves_parent_and_output(void)
{
    const uint8_t data[] = {0xA5, 0x5A};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    struct byte_flow *subflow = (struct byte_flow *)(uintptr_t)UINTPTR_MAX;
    CHECK(jpg_byteflow_get_next_n_bytes_subflow(reader, &subflow, 3) == JPG_BYTEFLOW_RANGE);
    CHECK(subflow == (struct byte_flow *)(uintptr_t)UINTPTR_MAX);

    uint8_t value = 0;
    CHECK(jpg_byteflow_get_next_byte(reader, &value) == JPG_BYTEFLOW_RET_OK);
    CHECK(value == 0xA5);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_skip_advances_cursor(void)
{
    const uint8_t data[] = {0x11, 0x22, 0x33, 0x44};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    CHECK(jpg_byteflow_skip_next_n_bytes(reader, 3) == JPG_BYTEFLOW_RET_OK);

    uint8_t value = 0;
    CHECK(jpg_byteflow_get_next_byte(reader, &value) == JPG_BYTEFLOW_RET_OK);
    CHECK(value == 0x44);
    CHECK(jpg_byteflow_get_next_byte(reader, &value) == JPG_BYTEFLOW_END_OF_FLOW);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_skip_range_error_preserves_cursor(void)
{
    const uint8_t data[] = {0x11, 0x22};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    CHECK(jpg_byteflow_skip_next_n_bytes(reader, 3) == JPG_BYTEFLOW_RANGE);

    uint16_t value = 0;
    CHECK(jpg_byteflow_get_next_bytes_u16(reader, &value) == JPG_BYTEFLOW_RET_OK);
    CHECK(value == UINT16_C(0x1122));
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_remain_len_tracks_cursor(void)
{
    const uint8_t data[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    size_t remaining = 0;
    uint8_t byte = 0;
    uint16_t word = 0;
    const uint8_t *span = NULL;

    CHECK(jpg_byteflow_get_remain_len(reader, &remaining) == JPG_BYTEFLOW_RET_OK);
    CHECK(remaining == sizeof(data));
    CHECK(jpg_byteflow_get_next_byte(reader, &byte) == JPG_BYTEFLOW_RET_OK);
    CHECK(jpg_byteflow_get_remain_len(reader, &remaining) == JPG_BYTEFLOW_RET_OK);
    CHECK(remaining == 6);
    CHECK(jpg_byteflow_get_next_bytes_u16(reader, &word) == JPG_BYTEFLOW_RET_OK);
    CHECK(jpg_byteflow_get_remain_len(reader, &remaining) == JPG_BYTEFLOW_RET_OK);
    CHECK(remaining == 4);
    CHECK(jpg_byteflow_get_next_n_bytes_span(reader, &span, 2) == JPG_BYTEFLOW_RET_OK);
    CHECK(jpg_byteflow_get_remain_len(reader, &remaining) == JPG_BYTEFLOW_RET_OK);
    CHECK(remaining == 2);
    CHECK(jpg_byteflow_skip_next_n_bytes(reader, 2) == JPG_BYTEFLOW_RET_OK);
    CHECK(jpg_byteflow_get_remain_len(reader, &remaining) == JPG_BYTEFLOW_RET_OK);
    CHECK(remaining == 0);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_u16_reports_end_of_flow_at_end(void)
{
    const uint8_t data[] = {0xA5};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    uint8_t byte = 0;
    uint16_t word = UINT16_C(0xBEEF);
    CHECK(jpg_byteflow_get_next_byte(reader, &byte) == JPG_BYTEFLOW_RET_OK);
    CHECK(jpg_byteflow_get_next_bytes_u16(reader, &word) == JPG_BYTEFLOW_END_OF_FLOW);
    CHECK(word == UINT16_C(0xBEEF));
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_reader_references_input_buffer(void)
{
    uint8_t data[] = {0x10};
    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);

    data[0] = 0xA5;

    uint8_t value = 0;
    CHECK(jpg_byteflow_get_next_byte(reader, &value) == JPG_BYTEFLOW_RET_OK);
    CHECK(value == 0xA5);
    jpg_byteflow_deallocate(reader);
    return 1;
}

static int test_rejects_invalid_arguments(void)
{
    const uint8_t data[] = {0x01, 0x02};
    uint8_t byte = 0;
    uint16_t word = 0;
    const uint8_t *span = NULL;
    struct byte_flow *subflow = NULL;

    CHECK(jpg_byteflow_allocate(NULL, sizeof(data)) == NULL);
    CHECK(jpg_byteflow_allocate(data, 0) == NULL);
    jpg_byteflow_deallocate(NULL);

    CHECK(jpg_byteflow_get_next_byte(NULL, &byte) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_bytes_u16(NULL, &word) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_n_bytes(NULL, &byte, 1) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_n_bytes_span(NULL, NULL, 1) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_skip_next_n_bytes(NULL, 1) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_remain_len(NULL, NULL) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_n_bytes_subflow(NULL, NULL, 1) == JPG_BYTEFLOW_BAD_ARG);

    struct byte_flow *reader = jpg_byteflow_allocate(data, sizeof(data));
    CHECK(reader != NULL);
    CHECK(jpg_byteflow_get_next_byte(reader, NULL) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_bytes_u16(reader, NULL) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_n_bytes(reader, NULL, 1) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_n_bytes(reader, &byte, 0) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_n_bytes_span(reader, NULL, 1) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_n_bytes_span(reader, &span, 0) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_skip_next_n_bytes(reader, 0) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_remain_len(reader, NULL) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_n_bytes_subflow(reader, NULL, 1) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(jpg_byteflow_get_next_n_bytes_subflow(reader, &subflow, 0) == JPG_BYTEFLOW_BAD_ARG);
    CHECK(subflow == NULL);
    jpg_byteflow_deallocate(reader);
    return 1;
}

int main(void)
{
    const test_case_t tests[] = {
        {"bytes are read in order", test_reads_bytes_in_order},
        {"u16 values use JPEG big-endian order", test_reads_u16_big_endian},
        {"failed u16 read preserves the cursor", test_u16_range_error_does_not_advance_cursor},
        {"multiple bytes are read as one operation", test_reads_multiple_bytes},
        {"failed bulk read preserves output and cursor", test_bulk_range_error_does_not_modify_state},
        {"bulk read reports end of flow", test_bulk_read_reports_end_of_flow},
        {"bulk, span and skip can consume the exact remainder", test_operations_can_consume_remaining_bytes},
        {"span read returns data and advances the cursor", test_span_returns_data_and_advances_cursor},
        {"failed span read preserves output and cursor", test_span_range_error_preserves_cursor_and_output},
        {"subflow is bounded and advances its parent", test_subflow_is_bounded_and_advances_parent},
        {"failed subflow read preserves output and parent", test_subflow_error_preserves_parent_and_output},
        {"skip advances the cursor", test_skip_advances_cursor},
        {"failed skip preserves the cursor", test_skip_range_error_preserves_cursor},
        {"remaining length tracks the cursor", test_remain_len_tracks_cursor},
        {"u16 read reports end of flow at the end", test_u16_reports_end_of_flow_at_end},
        {"reader references its input buffer", test_reader_references_input_buffer},
        {"invalid arguments are rejected", test_rejects_invalid_arguments},
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

    printf("All %zu byteflow tests passed\n", test_count);
    return 0;
}
