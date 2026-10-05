#include "huffman.h"
#include "log.h"
#include <stdbool.h>
#include <stdarg.h>

static bool s_huffman_add_value(tree_node_t *cur_node, int current_depth, int target_depth, uint8_t value);

huffman_tree_t *huffman_tree_create(uint8_t tree_class, uint8_t id, uint8_t *codes_cnt_array, uint8_t *values, uint8_t values_cnt)
{
    huffman_tree_t *tree = calloc(1, sizeof(huffman_tree_t));
    tree->id = id;
    tree->tree_class = tree_class;
    tree->tree = calloc(1, sizeof(tree_node_t));
    tree->tree->leaf_node = false;
    
    int codes_cnt_index = 0;
    for(int i = 0; i < 16; i++){
        if (codes_cnt_array[i] == 0)
            continue;
        for(int j = 0; j < codes_cnt_array[i]; j++){
            s_huffman_add_value(tree->tree, -1, i+1, values[codes_cnt_index]);
            codes_cnt_index++;
        }
    }


    return tree;
}

void print_binary_16bit(uint16_t value , char *out_str)
{
    for (int i = 15; i >=0; i--)
    {
        out_str[15-i] = (value & 1<<i) ? '1' : '0';
    }
    out_str[16] = '\0';
}

void print_binary_8bit(uint8_t value , char *out_str)
{
    for (int i = 7; i >=0; i--)
    {
        out_str[7-i] = (value & 1<<i) ? '1' : '0';
    }
    out_str[8] = '\0';
}

int s_searcher (tree_node_t* node, uint16_t cur_code)
{
    if (node->leaf_node){
        char str[17] = {};
        print_binary_16bit(cur_code , str);
        log_str(LOG_LEVEL_DEBUG, "%s = %X\n", str, node->value);
        return 0;
    }
    if (node->left)
        s_searcher(node->left, cur_code << 1);
    if (node->right)
        s_searcher(node->right, (cur_code << 1)|1);
    return 0;
}

void huffman_tree_dump(huffman_tree_t *tree)
{
    log_str(LOG_LEVEL_DEBUG, "Huffman codes dump for tree with id=%d and class %s:\n", tree->id, tree->tree_class ? "AC" : "DC");
    uint16_t l_value = 0;
    if (tree->tree->left)
        s_searcher(tree->tree->left, l_value << 1);
    if (tree->tree->right)
        s_searcher(tree->tree->right, (l_value << 1)|1);
}

static bool s_huffman_add_value(tree_node_t *cur_node, int current_depth, int target_depth, uint8_t value)
{
    if (current_depth++ > 15)
        return false;
    
    if (cur_node->leaf_node)
        return false;

    if (current_depth == target_depth){
        cur_node->leaf_node = true;
        cur_node->value = value;
        return true;
    } else {
        if (!cur_node->left){
            cur_node->left = calloc(1, sizeof(tree_node_t));
            if (!s_huffman_add_value(cur_node->left, current_depth, target_depth, value)){
                if (!cur_node->right){
                    cur_node->right = calloc(1, sizeof(tree_node_t));
                    return s_huffman_add_value(cur_node->right, current_depth, target_depth, value);
                } else if (!cur_node->right->leaf_node) {
                    return s_huffman_add_value(cur_node->right, current_depth, target_depth, value);
                }
            } else {
                return true;
            }
        } else if (!cur_node->left->leaf_node){
            if (!s_huffman_add_value(cur_node->left, current_depth, target_depth, value)){
                if (!cur_node->right){
                    cur_node->right = calloc(1, sizeof(tree_node_t));
                    return s_huffman_add_value(cur_node->right, current_depth, target_depth, value);
                } else if (!cur_node->right->leaf_node) {
                    return s_huffman_add_value(cur_node->right, current_depth, target_depth, value);
                }
            } else {
                return true;
            }
        } else if (!cur_node->right){
            cur_node->right = calloc(1, sizeof(tree_node_t));
            return s_huffman_add_value(cur_node->right, current_depth, target_depth, value);
        } else if (!cur_node->right->leaf_node) {
            return s_huffman_add_value(cur_node->right, current_depth, target_depth, value);
        }
    }
        
    return false;
}