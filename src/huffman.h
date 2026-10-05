#ifndef PICTURE_LIB_HUFFMAN_H
#define PICTURE_LIB_HUFFMAN_H

#include "stdlib.h"
#include "string.h"
#include "stdbool.h"
#include <stdio.h>
#include <stdint.h>

typedef struct tree_node{
    uint8_t value;
    bool leaf_node;
    struct tree_node *left;
    struct tree_node *right;
} tree_node_t;

typedef struct huffman_tree {
    uint8_t id; // tree id
    uint8_t tree_class; // AC if 1 or DC if 0
    tree_node_t *tree;
} huffman_tree_t;


huffman_tree_t *huffman_tree_create(uint8_t tree_class, uint8_t id, uint8_t *codes_cnt_array, uint8_t *values, uint8_t values_cnt);
void huffman_tree_dump(huffman_tree_t *tree);
void print_binary_16bit(uint16_t value , char *out_str);
void print_binary_8bit(uint8_t value , char *out_str);

#endif /* PICTURE_LIB_HUFFMAN_H */
