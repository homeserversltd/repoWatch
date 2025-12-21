#include "json-utils.h"

int main() {
    printf("Testing JSON parsing...\n");

    // Check if index.json exists
    FILE* check = fopen("index.json", "r");
    if (check) {
        printf("index.json exists\n");
        fclose(check);
    } else {
        printf("index.json does not exist\n");
    }

    json_value_t* root = json_parse_file("git-submodules.report");
    if (!root) {
        printf("Failed to load git-submodules.report\n");
        return 1;
    }

    printf("Successfully loaded JSON\n");

    // Check if it's an object and has repositories array
    if (root->type == JSON_OBJECT) {
        printf("Root is JSON object\n");

        // Look for repositories array
        for (size_t i = 0; i < root->value.obj_val->count; i++) {
            json_entry_t* entry = root->value.obj_val->entries[i];
            printf("Key: %s, Type: %d\n", entry->key, entry->value->type);

            if (strcmp(entry->key, "repositories") == 0 && entry->value->type == JSON_ARRAY) {
                printf("Found repositories array with %zu items\n", entry->value->value.arr_val->count);
            }
        }
    } else {
        printf("Root is not a JSON object\n");
    }

    size_t count = 0;
    char** children = json_get_children(root, &count);

    if (children) {
        printf("Found %zu children:\n", count);
        for (size_t i = 0; i < count; i++) {
            printf("  %s\n", children[i]);
            free(children[i]);
        }
        free(children);
    } else {
        printf("No children found\n");
    }

    json_free(root);
    return 0;
}
