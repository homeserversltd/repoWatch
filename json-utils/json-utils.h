#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// JSON value types
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

// Forward declarations
typedef struct json_value json_value_t;
typedef struct json_array json_array_t;
typedef struct json_object json_object_t;

// JSON value structure
struct json_value {
    json_type_t type;
    union {
        int bool_val;
        double num_val;
        char* str_val;
        json_array_t* arr_val;
        json_object_t* obj_val;
    } value;
};

// JSON array structure
struct json_array {
    json_value_t** items;
    size_t count;
    size_t capacity;
};

// JSON object entry
typedef struct {
    char* key;
    json_value_t* value;
} json_entry_t;

// JSON object structure
struct json_object {
    json_entry_t** entries;
    size_t count;
    size_t capacity;
};

// JSON parsing functions
json_value_t* json_parse_file(const char* filename);
json_value_t* json_parse_string(const char* json_str);
void json_free(json_value_t* value);

// JSON writing functions
char* json_stringify(json_value_t* value);
int json_write_file(const char* filename, json_value_t* value);

// JSON creation functions
json_value_t* json_create_null();
json_value_t* json_create_bool(int value);
json_value_t* json_create_number(double value);
json_value_t* json_create_string(const char* value);
json_value_t* json_create_array();
json_value_t* json_create_object();

// JSON array manipulation
int json_array_add(json_value_t* array, json_value_t* value);

// JSON object manipulation
int json_object_set(json_value_t* object, const char* key, json_value_t* value);

// Utility functions for index.json structure
char** json_get_children(json_value_t* root, size_t* count);
json_value_t* json_get_child_config(json_value_t* root, const char* child_name);
int json_update_children(json_value_t* root, char** children, size_t count);

// Specialized index.json functions
json_value_t* index_json_load(const char* path);
char** index_json_get_children(const char* path, size_t* count);
int index_json_update_children(const char* path, char** children, size_t count);

// File tree processing functions
typedef struct file_tree_node {
    char* name;                    // Directory or file name
    struct file_tree_node** children;
    size_t child_count;
    int is_file;                   // 1 if this is a file, 0 if directory
} file_tree_node_t;

typedef struct {
    char* repo_name;
    char* repo_path;
    file_tree_node_t* root;
} file_tree_repo_t;

typedef struct {
    file_tree_repo_t* repos;
    size_t repo_count;
} file_tree_report_t;

file_tree_report_t* json_process_dirty_files_to_tree(json_value_t* dirty_files_report);
void file_tree_free(file_tree_report_t* report);
void file_tree_node_free(file_tree_node_t* node);

// Memory management helpers
void json_array_free(json_array_t* arr);
void json_object_free(json_object_t* obj);

#endif // JSON_UTILS_H
