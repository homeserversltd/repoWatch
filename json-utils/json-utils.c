#include "json-utils.h"
#include <unistd.h>
#include <fcntl.h>

// Skip whitespace
static const char* skip_whitespace(const char* str) {
    while (*str && isspace(*str)) str++;
    return str;
}

// Forward declarations for parsing functions
static json_value_t* parse_value(const char** json);
static json_object_t* parse_object(const char** json);
static json_array_t* parse_array(const char** json);
static char* parse_string(const char** json);

// Parse any JSON value
static json_value_t* parse_value(const char** json) {
    *json = skip_whitespace(*json);

    if (**json == '{') {
        json_object_t* obj = parse_object(json);
        if (!obj) return NULL;

        json_value_t* result = calloc(1, sizeof(json_value_t));
        if (!result) {
            json_object_free(obj);
            return NULL;
        }

        result->type = JSON_OBJECT;
        result->value.obj_val = obj;
        return result;
    } else if (**json == '[') {
        json_array_t* arr = parse_array(json);
        if (!arr) return NULL;

        json_value_t* result = calloc(1, sizeof(json_value_t));
        if (!result) {
            json_array_free(arr);
            return NULL;
        }

        result->type = JSON_ARRAY;
        result->value.arr_val = arr;
        return result;
    } else if (**json == '"') {
        char* str = parse_string(json);
        if (!str) return NULL;

        json_value_t* result = calloc(1, sizeof(json_value_t));
        if (!result) {
            free(str);
            return NULL;
        }

        result->type = JSON_STRING;
        result->value.str_val = str;
        return result;
    } else if (**json >= '0' && **json <= '9' || **json == '-') {
        // Number
        const char* start = *json;
        if (**json == '-') (*json)++;

        while (**json >= '0' && **json <= '9') (*json)++;
        if (**json == '.') {
            (*json)++;
            while (**json >= '0' && **json <= '9') (*json)++;
        }
        if (**json == 'e' || **json == 'E') {
            (*json)++;
            if (**json == '+' || **json == '-') (*json)++;
            while (**json >= '0' && **json <= '9') (*json)++;
        }

        char* num_str = strndup(start, *json - start);
        if (!num_str) return NULL;

        json_value_t* result = calloc(1, sizeof(json_value_t));
        if (!result) {
            free(num_str);
            return NULL;
        }

        result->type = JSON_NUMBER;
        result->value.num_val = atof(num_str);
        free(num_str);
        return result;
    } else if (strncmp(*json, "true", 4) == 0) {
        *json += 4;
        json_value_t* result = calloc(1, sizeof(json_value_t));
        if (!result) return NULL;
        result->type = JSON_BOOL;
        result->value.bool_val = 1;
        return result;
    } else if (strncmp(*json, "false", 5) == 0) {
        *json += 5;
        json_value_t* result = calloc(1, sizeof(json_value_t));
        if (!result) return NULL;
        result->type = JSON_BOOL;
        result->value.bool_val = 0;
        return result;
    } else if (strncmp(*json, "null", 4) == 0) {
        *json += 4;
        json_value_t* result = calloc(1, sizeof(json_value_t));
        if (!result) return NULL;
        result->type = JSON_NULL;
        return result;
    }

    return NULL;
}

// Parse JSON string
static char* parse_string(const char** json) {
    if (**json != '"') return NULL;
    (*json)++; // Skip opening quote

    const char* start = *json;
    while (**json && **json != '"') {
        if (**json == '\\') (*json)++; // Skip escaped chars
        (*json)++;
    }

    if (**json != '"') return NULL;

    size_t len = *json - start;
    char* result = malloc(len + 1);
    if (!result) return NULL;

    strncpy(result, start, len);
    result[len] = '\0';

    (*json)++; // Skip closing quote
    return result;
}

// Parse JSON array
static json_array_t* parse_array(const char** json) {
    if (**json != '[') return NULL;
    (*json)++; // Skip opening bracket

    json_array_t* arr = calloc(1, sizeof(json_array_t));
    if (!arr) return NULL;

    arr->capacity = 8;
    arr->items = malloc(sizeof(json_value_t*) * arr->capacity);
    if (!arr->items) {
        free(arr);
        return NULL;
    }

    *json = skip_whitespace(*json);
    if (**json == ']') {
        (*json)++; // Empty array
        return arr;
    }

    while (**json && **json != ']') {
        // Parse any JSON value
        json_value_t* val = parse_value(json);
        if (!val) {
            json_array_free(arr);
            return NULL;
        }

        // Resize array if needed
        if (arr->count >= arr->capacity) {
            arr->capacity *= 2;
            arr->items = realloc(arr->items, sizeof(json_value_t*) * arr->capacity);
            if (!arr->items) {
                json_free(val);
                json_array_free(arr);
                return NULL;
            }
        }

        arr->items[arr->count++] = val;

        *json = skip_whitespace(*json);
        if (**json == ',') {
            (*json)++;
            *json = skip_whitespace(*json);
        } else if (**json != ']') {
            json_array_free(arr);
            return NULL;
        }
    }

    if (**json != ']') {
        json_array_free(arr);
        return NULL;
    }
    (*json)++; // Skip closing bracket

    return arr;
}

// Parse JSON object (simplified for index.json structure)
static json_object_t* parse_object(const char** json) {
    if (**json != '{') return NULL;
    (*json)++; // Skip opening brace

    json_object_t* obj = calloc(1, sizeof(json_object_t));
    if (!obj) return NULL;

    obj->capacity = 16;
    obj->entries = malloc(sizeof(json_entry_t*) * obj->capacity);
    if (!obj->entries) {
        free(obj);
        return NULL;
    }

    *json = skip_whitespace(*json);
    while (**json && **json != '}') {
        // Parse key
        if (**json != '"') {
            json_object_free(obj);
            return NULL;
        }

        char* key = parse_string(json);
        if (!key) {
            json_object_free(obj);
            return NULL;
        }

        *json = skip_whitespace(*json);
        if (**json != ':') {
            free(key);
            json_object_free(obj);
            return NULL;
        }
        (*json)++; // Skip colon

        *json = skip_whitespace(*json);

        // Parse value using the unified parse_value function
        json_value_t* value = parse_value(json);

        if (!value) {
            free(key);
            json_object_free(obj);
            return NULL;
        }

        // Add entry
        if (obj->count >= obj->capacity) {
            obj->capacity *= 2;
            obj->entries = realloc(obj->entries, sizeof(json_entry_t*) * obj->capacity);
            if (!obj->entries) {
                free(key);
                json_free(value);
                json_object_free(obj);
                return NULL;
            }
        }

        json_entry_t* entry = malloc(sizeof(json_entry_t));
        if (!entry) {
            free(key);
            json_free(value);
            json_object_free(obj);
            return NULL;
        }

        entry->key = key;
        entry->value = value;
        obj->entries[obj->count++] = entry;

        *json = skip_whitespace(*json);
        if (**json == ',') {
            (*json)++;
            *json = skip_whitespace(*json);
        } else if (**json != '}') {
            json_object_free(obj);
            return NULL;
        }
    }

    if (**json != '}') {
        json_object_free(obj);
        return NULL;
    }
    (*json)++; // Skip closing brace

    return obj;
}

// Public API implementations
json_value_t* json_parse_string(const char* json_str) {
    const char* ptr = skip_whitespace(json_str);

    if (*ptr == '{') {
        json_object_t* obj = parse_object(&ptr);
        if (!obj) return NULL;

        json_value_t* result = calloc(1, sizeof(json_value_t));
        if (!result) {
            json_object_free(obj);
            return NULL;
        }

        result->type = JSON_OBJECT;
        result->value.obj_val = obj;
        return result;
    } else if (*ptr == '[') {
        json_array_t* arr = parse_array(&ptr);
        if (!arr) return NULL;

        json_value_t* result = calloc(1, sizeof(json_value_t));
        if (!result) {
            json_array_free(arr);
            return NULL;
        }

        result->type = JSON_ARRAY;
        result->value.arr_val = arr;
        return result;
    }

    return NULL;
}

json_value_t* json_parse_file(const char* filename) {
    FILE* fp = fopen(filename, "r");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);


    json_value_t* result = json_parse_string(content);
    free(content);

    return result;
}

void json_free(json_value_t* value) {
    if (!value) return;

    switch (value->type) {
        case JSON_STRING:
            free(value->value.str_val);
            break;
        case JSON_ARRAY:
            json_array_free(value->value.arr_val);
            break;
        case JSON_OBJECT:
            json_object_free(value->value.obj_val);
            break;
        default:
            break;
    }

    free(value);
}

void json_array_free(json_array_t* arr) {
    if (!arr) return;

    for (size_t i = 0; i < arr->count; i++) {
        json_free(arr->items[i]);
    }

    free(arr->items);
    free(arr);
}

void json_object_free(json_object_t* obj) {
    if (!obj) return;

    for (size_t i = 0; i < obj->count; i++) {
        free(obj->entries[i]->key);
        json_free(obj->entries[i]->value);
        free(obj->entries[i]);
    }

    free(obj->entries);
    free(obj);
}

// Specialized functions for index.json
char** json_get_children(json_value_t* root, size_t* count) {
    if (!root || root->type != JSON_OBJECT) return NULL;

    // Find children array
    for (size_t i = 0; i < root->value.obj_val->count; i++) {
        json_entry_t* entry = root->value.obj_val->entries[i];
        if (strcmp(entry->key, "children") == 0 && entry->value->type == JSON_ARRAY) {
            json_array_t* arr = entry->value->value.arr_val;
            *count = arr->count;

            char** result = malloc(sizeof(char*) * arr->count);
            if (!result) return NULL;

            for (size_t j = 0; j < arr->count; j++) {
                if (arr->items[j]->type == JSON_STRING) {
                    result[j] = strdup(arr->items[j]->value.str_val);
                    if (!result[j]) {
                        // Cleanup on error
                        for (size_t k = 0; k < j; k++) free(result[k]);
                        free(result);
                        return NULL;
                    }
                } else {
                    // Cleanup on error
                    for (size_t k = 0; k < j; k++) free(result[k]);
                    free(result);
                    return NULL;
                }
            }

            return result;
        }
    }

    return NULL;
}

json_value_t* index_json_load(const char* path) {
    char filepath[1024];
    if (path && strcmp(path, ".") != 0) {
        snprintf(filepath, sizeof(filepath), "%s/index.json", path);
    } else {
        strcpy(filepath, "index.json");
    }

    return json_parse_file(filepath);
}

char** index_json_get_children(const char* path, size_t* count) {
    json_value_t* root = index_json_load(path);
    if (!root) return NULL;

    char** children = json_get_children(root, count);
    json_free(root);

    return children;
}

int index_json_update_children(const char* path, char** children, size_t count) {
    // For now, this is a placeholder - full implementation would require
    // JSON writing capabilities. For the current use case, we'll keep
    // the children reading functionality working.
    (void)path;
    (void)children;
    (void)count;
    return -1; // Not implemented yet
}

char* json_stringify(json_value_t* value) {
    if (!value) return NULL;

    switch (value->type) {
        case JSON_NULL:
            return strdup("null");

        case JSON_BOOL:
            return value->value.bool_val ? strdup("true") : strdup("false");

        case JSON_NUMBER: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.17g", value->value.num_val);
            return strdup(buf);
        }

        case JSON_STRING: {
            if (!value->value.str_val) return strdup("\"\"");

            // Calculate size needed for escaped string
            size_t len = strlen(value->value.str_val);
            size_t escaped_len = len * 2 + 3; // worst case: every char escaped + quotes + null
            char* result = malloc(escaped_len);
            if (!result) return NULL;

            char* out = result;
            *out++ = '"';

            for (const char* p = value->value.str_val; *p; p++) {
                switch (*p) {
                    case '"': *out++ = '\\'; *out++ = '"'; break;
                    case '\\': *out++ = '\\'; *out++ = '\\'; break;
                    case '\b': *out++ = '\\'; *out++ = 'b'; break;
                    case '\f': *out++ = '\\'; *out++ = 'f'; break;
                    case '\n': *out++ = '\\'; *out++ = 'n'; break;
                    case '\r': *out++ = '\\'; *out++ = 'r'; break;
                    case '\t': *out++ = '\\'; *out++ = 't'; break;
                    default:
                        if (*p < 32) {
                            // Control characters - escape as \uXXXX
                            out += sprintf(out, "\\u%04x", (unsigned char)*p);
                        } else {
                            *out++ = *p;
                        }
                        break;
                }
            }

            *out++ = '"';
            *out = '\0';
            return result;
        }

        case JSON_ARRAY: {
            json_array_t* arr = value->value.arr_val;
            if (!arr) return strdup("[]");

            size_t total_size = 3; // [] and null
            char** items = malloc(arr->count * sizeof(char*));
            if (!items) return NULL;

            for (size_t i = 0; i < arr->count; i++) {
                items[i] = json_stringify(arr->items[i]);
                if (!items[i]) {
                    for (size_t j = 0; j < i; j++) free(items[j]);
                    free(items);
                    return NULL;
                }
                total_size += strlen(items[i]) + 1; // + comma or closing bracket
            }

            char* result = malloc(total_size);
            if (!result) {
                for (size_t i = 0; i < arr->count; i++) free(items[i]);
                free(items);
                return NULL;
            }

            char* out = result;
            *out++ = '[';

            for (size_t i = 0; i < arr->count; i++) {
                if (i > 0) *out++ = ',';
                strcpy(out, items[i]);
                out += strlen(items[i]);
                free(items[i]);
            }

            *out++ = ']';
            *out = '\0';

            free(items);
            return result;
        }

        case JSON_OBJECT: {
            json_object_t* obj = value->value.obj_val;
            if (!obj) return strdup("{}");

            size_t total_size = 3; // {} and null
            char** keys = malloc(obj->count * sizeof(char*));
            char** values = malloc(obj->count * sizeof(char*));
            if (!keys || !values) {
                free(keys);
                free(values);
                return NULL;
            }

            for (size_t i = 0; i < obj->count; i++) {
                json_entry_t* entry = obj->entries[i];

                // Stringify key
                size_t key_len = strlen(entry->key) * 2 + 3; // escaped + quotes + null
                keys[i] = malloc(key_len);
                if (!keys[i]) goto object_error;

                char* key_out = keys[i];
                *key_out++ = '"';
                for (const char* p = entry->key; *p; p++) {
                    if (*p == '"' || *p == '\\') *key_out++ = '\\';
                    *key_out++ = *p;
                }
                *key_out++ = '"';
                *key_out = '\0';

                // Stringify value
                values[i] = json_stringify(entry->value);
                if (!values[i]) {
                    free(keys[i]);
                    goto object_error;
                }

                total_size += strlen(keys[i]) + strlen(values[i]) + 3; // "key":value,
            }

            char* result = malloc(total_size);
            if (!result) goto object_error;

            char* out = result;
            *out++ = '{';

            for (size_t i = 0; i < obj->count; i++) {
                if (i > 0) *out++ = ',';
                strcpy(out, keys[i]);
                out += strlen(keys[i]);
                *out++ = ':';
                strcpy(out, values[i]);
                out += strlen(values[i]);
                free(keys[i]);
                free(values[i]);
            }

            *out++ = '}';
            *out = '\0';

            free(keys);
            free(values);
            return result;

        object_error:
            for (size_t i = 0; i < obj->count; i++) {
                free(keys[i]);
                free(values[i]);
            }
            free(keys);
            free(values);
            return NULL;
        }

        default:
            return strdup("null");
    }
}

int json_write_file(const char* filename, json_value_t* value) {
    if (!filename || !value) return -1;

    char* json_str = json_stringify(value);
    if (!json_str) return -1;

    FILE* fp = fopen(filename, "w");
    if (!fp) {
        free(json_str);
        return -1;
    }

    fprintf(fp, "%s\n", json_str);
    fclose(fp);
    free(json_str);

    return 0;
}

json_value_t* json_get_child_config(json_value_t* root, const char* child_name) {
    // Placeholder - not implemented yet
    (void)root;
    (void)child_name;
    return NULL;
}

int json_update_children(json_value_t* root, char** children, size_t count) {
    // Placeholder - not implemented yet
    (void)root;
    (void)children;
    (void)count;
    return -1;
}

// JSON creation functions
json_value_t* json_create_null() {
    json_value_t* value = calloc(1, sizeof(json_value_t));
    if (!value) return NULL;
    value->type = JSON_NULL;
    return value;
}

json_value_t* json_create_bool(int bool_val) {
    json_value_t* value = calloc(1, sizeof(json_value_t));
    if (!value) return NULL;
    value->type = JSON_BOOL;
    value->value.bool_val = bool_val;
    return value;
}

json_value_t* json_create_number(double num_val) {
    json_value_t* value = calloc(1, sizeof(json_value_t));
    if (!value) return NULL;
    value->type = JSON_NUMBER;
    value->value.num_val = num_val;
    return value;
}

json_value_t* json_create_string(const char* str_val) {
    json_value_t* value = calloc(1, sizeof(json_value_t));
    if (!value) return NULL;
    value->type = JSON_STRING;
    value->value.str_val = str_val ? strdup(str_val) : NULL;
    return value;
}

json_value_t* json_create_array() {
    json_value_t* value = calloc(1, sizeof(json_value_t));
    if (!value) return NULL;

    json_array_t* arr = calloc(1, sizeof(json_array_t));
    if (!arr) {
        free(value);
        return NULL;
    }

    arr->capacity = 8;
    arr->items = calloc(arr->capacity, sizeof(json_value_t*));
    if (!arr->items) {
        free(arr);
        free(value);
        return NULL;
    }

    value->type = JSON_ARRAY;
    value->value.arr_val = arr;
    return value;
}

json_value_t* json_create_object() {
    json_value_t* value = calloc(1, sizeof(json_value_t));
    if (!value) return NULL;

    json_object_t* obj = calloc(1, sizeof(json_object_t));
    if (!obj) {
        free(value);
        return NULL;
    }

    obj->capacity = 8;
    obj->entries = calloc(obj->capacity, sizeof(json_entry_t*));
    if (!obj->entries) {
        free(obj);
        free(value);
        return NULL;
    }

    value->type = JSON_OBJECT;
    value->value.obj_val = obj;
    return value;
}

// JSON array manipulation
int json_array_add(json_value_t* array, json_value_t* value) {
    if (!array || array->type != JSON_ARRAY || !value) return -1;

    json_array_t* arr = array->value.arr_val;

    if (arr->count >= arr->capacity) {
        arr->capacity *= 2;
        json_value_t** new_items = realloc(arr->items, arr->capacity * sizeof(json_value_t*));
        if (!new_items) return -1;
        arr->items = new_items;
    }

    arr->items[arr->count] = value;
    arr->count++;

    return 0;
}

// JSON object manipulation
int json_object_set(json_value_t* object, const char* key, json_value_t* value) {
    if (!object || object->type != JSON_OBJECT || !key || !value) return -1;

    json_object_t* obj = object->value.obj_val;

    // Check if key already exists, if so replace value
    for (size_t i = 0; i < obj->count; i++) {
        if (strcmp(obj->entries[i]->key, key) == 0) {
            json_free(obj->entries[i]->value);
            obj->entries[i]->value = value;
            return 0;
        }
    }

    // Add new entry
    if (obj->count >= obj->capacity) {
        obj->capacity *= 2;
        json_entry_t** new_entries = realloc(obj->entries, obj->capacity * sizeof(json_entry_t*));
        if (!new_entries) return -1;
        obj->entries = new_entries;
    }

    json_entry_t* entry = calloc(1, sizeof(json_entry_t));
    if (!entry) return -1;

    entry->key = strdup(key);
    entry->value = value;

    if (!entry->key) {
        free(entry);
        return -1;
    }

    obj->entries[obj->count] = entry;
    obj->count++;

    return 0;
}

// File tree processing functions

// Create a new file tree node
static file_tree_node_t* file_tree_node_create(const char* name, int is_file) {
    file_tree_node_t* node = calloc(1, sizeof(file_tree_node_t));
    if (!node) return NULL;

    node->name = strdup(name);
    node->is_file = is_file;
    node->children = NULL;
    node->child_count = 0;

    return node;
}

// Add a child to a file tree node
static int file_tree_node_add_child(file_tree_node_t* parent, file_tree_node_t* child) {
    parent->children = realloc(parent->children, sizeof(file_tree_node_t*) * (parent->child_count + 1));
    if (!parent->children) return -1;

    parent->children[parent->child_count++] = child;
    return 0;
}

// Find or create a child directory node
static file_tree_node_t* file_tree_find_or_create_dir(file_tree_node_t* parent, const char* dirname) {
    // Look for existing directory
    for (size_t i = 0; i < parent->child_count; i++) {
        if (!parent->children[i]->is_file && strcmp(parent->children[i]->name, dirname) == 0) {
            return parent->children[i];
        }
    }

    // Create new directory
    file_tree_node_t* dir_node = file_tree_node_create(dirname, 0);
    if (!dir_node) return NULL;

    if (file_tree_node_add_child(parent, dir_node) != 0) {
        file_tree_node_free(dir_node);
        return NULL;
    }

    return dir_node;
}

// Add a file path to the tree structure
static int file_tree_add_path(file_tree_node_t* root, const char* filepath) {
    if (!filepath || !*filepath) {
        // Empty path - add as direct file
        file_tree_node_t* file_node = file_tree_node_create(filepath, 1);
        if (!file_node) return -1;
        return file_tree_node_add_child(root, file_node);
    }

    char* path_copy = strdup(filepath);
    if (!path_copy) return -1;

    char* saveptr;
    char* token = strtok_r(path_copy, "/", &saveptr);
    file_tree_node_t* current = root;

    // Process directory parts
    while (token) {
        char* next_token = strtok_r(NULL, "/", &saveptr);
        if (!next_token) {
            // This is the filename
            file_tree_node_t* file_node = file_tree_node_create(token, 1);
            if (!file_node) {
                free(path_copy);
                return -1;
            }
            if (file_tree_node_add_child(current, file_node) != 0) {
                file_tree_node_free(file_node);
                free(path_copy);
                return -1;
            }
            break;
        } else {
            // This is a directory
            current = file_tree_find_or_create_dir(current, token);
            if (!current) {
                free(path_copy);
                return -1;
            }
        }
    }

    free(path_copy);
    return 0;
}

// Process a repository's files into a tree
static file_tree_node_t* file_tree_build_repo_tree(char** files, size_t file_count) {
    file_tree_node_t* root = file_tree_node_create("", 0); // Root has no name
    if (!root) return NULL;

    for (size_t i = 0; i < file_count; i++) {
        if (file_tree_add_path(root, files[i]) != 0) {
            file_tree_node_free(root);
            return NULL;
        }
    }

    return root;
}

// Main function to process dirty files report into tree structure
file_tree_report_t* json_process_dirty_files_to_tree(json_value_t* dirty_files_report) {
    if (!dirty_files_report || dirty_files_report->type != JSON_OBJECT) {
        return NULL;
    }

    file_tree_report_t* report = calloc(1, sizeof(file_tree_report_t));
    if (!report) return NULL;

    // Find repositories array
    json_object_t* root_obj = dirty_files_report->value.obj_val;
    json_value_t* repos_val = NULL;

    for (size_t i = 0; i < root_obj->count; i++) {
        if (strcmp(root_obj->entries[i]->key, "repositories") == 0) {
            repos_val = root_obj->entries[i]->value;
            break;
        }
    }

    if (!repos_val || repos_val->type != JSON_ARRAY) {
        free(report);
        return NULL;
    }

    json_array_t* repos_arr = repos_val->value.arr_val;
    report->repo_count = repos_arr->count;
    report->repos = calloc(repos_arr->count, sizeof(file_tree_repo_t));
    if (!report->repos) {
        free(report);
        return NULL;
    }

    // Process each repository
    for (size_t i = 0; i < repos_arr->count; i++) {
        json_value_t* repo_obj = repos_arr->items[i];
        if (repo_obj->type != JSON_OBJECT) continue;

        file_tree_repo_t* repo = &report->repos[i];
        json_object_t* repo_json = repo_obj->value.obj_val;

        // Extract repo name and path
        for (size_t j = 0; j < repo_json->count; j++) {
            json_entry_t* entry = repo_json->entries[j];

            if (strcmp(entry->key, "name") == 0 && entry->value->type == JSON_STRING) {
                repo->repo_name = strdup(entry->value->value.str_val);
            } else if (strcmp(entry->key, "path") == 0 && entry->value->type == JSON_STRING) {
                repo->repo_path = strdup(entry->value->value.str_val);
            } else if (strcmp(entry->key, "dirty_files") == 0 && entry->value->type == JSON_ARRAY) {
                json_array_t* files_arr = entry->value->value.arr_val;
                char** files = calloc(files_arr->count, sizeof(char*));
                size_t file_count = 0;

                for (size_t k = 0; k < files_arr->count; k++) {
                    if (files_arr->items[k]->type == JSON_STRING) {
                        files[file_count++] = strdup(files_arr->items[k]->value.str_val);
                    }
                }

                if (file_count > 0) {
                    repo->root = file_tree_build_repo_tree(files, file_count);
                }

                // Free temporary files array
                for (size_t k = 0; k < file_count; k++) {
                    free(files[k]);
                }
                free(files);
            }
        }
    }

    return report;
}

// Free file tree node recursively
void file_tree_node_free(file_tree_node_t* node) {
    if (!node) return;

    for (size_t i = 0; i < node->child_count; i++) {
        file_tree_node_free(node->children[i]);
    }

    free(node->children);
    free(node->name);
    free(node);
}

// Free file tree report
void file_tree_free(file_tree_report_t* report) {
    if (!report) return;

    for (size_t i = 0; i < report->repo_count; i++) {
        free(report->repos[i].repo_name);
        free(report->repos[i].repo_path);
        file_tree_node_free(report->repos[i].root);
    }

    free(report->repos);
    free(report);
}
