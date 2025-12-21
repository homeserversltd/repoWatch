#include "json-utils.h"
#include <unistd.h>
#include <string.h>

// Utility for reading repoWatch report files (JSON files at repoWatch root level)
// Unlike index_json_load which looks for index.json, this reads any JSON file directly

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <report_file> [key_path]\n", argv[0]);
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s git-submodules.report summary.total_dirty_repositories\n", argv[0]);
        fprintf(stderr, "  %s dirty-files-report.json repositories.0.name\n", argv[0]);
        fprintf(stderr, "  %s dirty-files-report.json (prints whole file)\n", argv[0]);
        return 1;
    }

    const char* report_file = argv[1];
    const char* key_path = NULL;

    if (argc > 2) {
        key_path = argv[2];
    }

    // Load the JSON report file directly
    json_value_t* root = json_parse_file(report_file);
    if (!root) {
        fprintf(stderr, "Error: Could not load JSON report from %s\n", report_file);
        return 1;
    }

    json_value_t* target = root;

    // If a key path is specified, navigate to it
    if (key_path) {
        target = get_nested_value(root, key_path);
        if (!target) {
            fprintf(stderr, "Error: Could not find value for key path '%s'\n", key_path);
            json_free(root);
            return 1;
        }
    }

    // Output the result
    switch (target->type) {
        case JSON_STRING:
            printf("%s\n", target->value.str_val);
            break;
        case JSON_BOOL:
            printf("%s\n", target->value.bool_val ? "true" : "false");
            break;
        case JSON_NUMBER:
            if (target->value.num_val == (int)target->value.num_val) {
                printf("%d\n", (int)target->value.num_val);
            } else {
                printf("%.2f\n", target->value.num_val);
            }
            break;
        case JSON_ARRAY: {
            json_array_t* arr = target->value.arr_val;
            for (size_t i = 0; i < arr->count; i++) {
                if (i > 0) printf(" ");
                if (arr->items[i]->type == JSON_STRING) {
                    printf("%s", arr->items[i]->value.str_val);
                } else {
                    printf("(non-string)");
                }
            }
            printf("\n");
            break;
        }
        case JSON_OBJECT:
            // For objects, stringify the JSON
            {
                char* json_str = json_stringify(target);
                if (json_str) {
                    printf("%s\n", json_str);
                    free(json_str);
                } else {
                    printf("(object)\n");
                }
            }
            break;
        default:
            printf("(unsupported type)\n");
            break;
    }

    json_free(root);
    return 0;
}

