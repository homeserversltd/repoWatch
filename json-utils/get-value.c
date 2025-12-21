#include "json-utils.h"
#include <unistd.h>

// Simple function to get nested JSON values
// Supports dot notation like "config.ui_refresh_rate"
json_value_t* get_nested_value(json_value_t* root, const char* key_path) {
    if (!root || root->type != JSON_OBJECT) return NULL;

    char* path_copy = strdup(key_path);
    if (!path_copy) return NULL;

    char* token = strtok(path_copy, ".");
    json_value_t* current = root;

    while (token && current && current->type == JSON_OBJECT) {
        int found = 0;
        for (size_t i = 0; i < current->value.obj_val->count; i++) {
            json_entry_t* entry = current->value.obj_val->entries[i];
            if (strcmp(entry->key, token) == 0) {
                current = entry->value;
                found = 1;
                break;
            }
        }
        if (!found) {
            current = NULL;
            break;
        }
        token = strtok(NULL, ".");
    }

    free(path_copy);
    return token ? NULL : current; // If we still have tokens, we didn't find the full path
}

// Separate main function for standalone utility
#ifndef GET_VALUE_LIBRARY_ONLY

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <key_path> [path]\n", argv[0]);
        fprintf(stderr, "Example: %s children .\n", argv[0]);
        fprintf(stderr, "Example: %s config.ui_refresh_rate\n", argv[0]);
        return 1;
    }

    const char* key_path = argv[1];
    const char* path = ".";

    if (argc > 2) {
        path = argv[2];
    }

    json_value_t* root = index_json_load(path);
    if (!root) {
        fprintf(stderr, "Error: Could not load index.json from %s\n", path);
        return 1;
    }

    json_value_t* value = get_nested_value(root, key_path);
    if (!value) {
        fprintf(stderr, "Error: Could not find value for key path '%s'\n", key_path);
        json_free(root);
        return 1;
    }

    // Output based on type
    switch (value->type) {
        case JSON_STRING:
            printf("%s\n", value->value.str_val);
            break;
        case JSON_BOOL:
            printf("%s\n", value->value.bool_val ? "true" : "false");
            break;
        case JSON_ARRAY: {
            json_array_t* arr = value->value.arr_val;
            for (size_t i = 0; i < arr->count; i++) {
                if (i > 0) printf(" ");
                if (arr->items[i]->type == JSON_STRING) {
                    printf("%s", arr->items[i]->value.str_val);
                }
            }
            printf("\n");
            break;
        }
        default:
            printf("(unsupported type)\n");
            break;
    }

    json_free(root);
    return 0;
}
#endif // GET_VALUE_LIBRARY_ONLY
