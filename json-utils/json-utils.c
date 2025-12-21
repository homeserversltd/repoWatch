#include "json-utils.h"
#include <unistd.h>
#include <fcntl.h>

// Skip whitespace
static const char* skip_whitespace(const char* str) {
    while (*str && isspace(*str)) str++;
    return str;
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

// Parse JSON array (simplified for string arrays)
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
    while (**json && **json != ']') {
        if (**json == '"') {
            // String value
            char* str = parse_string(json);
            if (!str) {
                json_array_free(arr);
                return NULL;
            }

            json_value_t* val = calloc(1, sizeof(json_value_t));
            if (!val) {
                free(str);
                json_array_free(arr);
                return NULL;
            }

            val->type = JSON_STRING;
            val->value.str_val = str;

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
        }

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

        json_value_t* value = NULL;

        // Parse value based on type
        if (**json == '"') {
            char* str = parse_string(json);
            if (str) {
                value = calloc(1, sizeof(json_value_t));
                if (value) {
                    value->type = JSON_STRING;
                    value->value.str_val = str;
                } else {
                    free(str);
                }
            }
        } else if (**json == '[') {
            json_array_t* arr = parse_array(json);
            if (arr) {
                value = calloc(1, sizeof(json_value_t));
                if (value) {
                    value->type = JSON_ARRAY;
                    value->value.arr_val = arr;
                } else {
                    json_array_free(arr);
                }
            }
        } else if (**json == '{') {
            json_object_t* child_obj = parse_object(json);
            if (child_obj) {
                value = calloc(1, sizeof(json_value_t));
                if (value) {
                    value->type = JSON_OBJECT;
                    value->value.obj_val = child_obj;
                } else {
                    json_object_free(child_obj);
                }
            }
        } else if (strncmp(*json, "true", 4) == 0) {
            value = calloc(1, sizeof(json_value_t));
            if (value) {
                value->type = JSON_BOOL;
                value->value.bool_val = 1;
            }
            *json += 4;
        } else if (strncmp(*json, "false", 5) == 0) {
            value = calloc(1, sizeof(json_value_t));
            if (value) {
                value->type = JSON_BOOL;
                value->value.bool_val = 0;
            }
            *json += 5;
        } else if (**json >= '0' && **json <= '9') {
            // Simple number parsing
            char* end;
            strtod(*json, &end);
            value = calloc(1, sizeof(json_value_t));
            if (value) {
                value->type = JSON_NUMBER;
                value->value.num_val = strtod(*json, &end);
            }
            *json = end;
        } else {
            printf("Debug: Unknown value type, char: '%c' (%d)\n", **json, (int)**json);
        }

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
    // Simplified stringify - only handles basic cases needed for index.json
    if (!value) return NULL;

    // This is a placeholder implementation
    // Full JSON stringify would be complex
    return strdup("{}");
}

int json_write_file(const char* filename, json_value_t* value) {
    // Placeholder - not implemented yet
    (void)filename;
    (void)value;
    return -1;
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
