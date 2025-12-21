#include "json-utils.h"
#include <unistd.h>

int main(int argc, char* argv[]) {
    const char* path = ".";

    if (argc > 1) {
        path = argv[1];
    }

    size_t count = 0;
    char** children = index_json_get_children(path, &count);

    if (!children) {
        fprintf(stderr, "Error: Could not read children from index.json\n");
        return 1;
    }

    // Output format: space-separated list
    for (size_t i = 0; i < count; i++) {
        if (i > 0) printf(" ");
        printf("%s", children[i]);
        free(children[i]);
    }
    printf("\n");

    free(children);
    return 0;
}

