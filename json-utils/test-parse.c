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

    json_value_t* root = index_json_load(".");
    if (!root) {
        printf("Failed to load index.json\n");
        return 1;
    }

    printf("Successfully loaded JSON\n");

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
