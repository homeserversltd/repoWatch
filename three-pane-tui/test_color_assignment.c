#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mock extract_repo_name_from_header function for testing
char* extract_repo_name_from_header(const char* item) {
    if (!item) return NULL;
    const char* prefix = "Repository: ";
    size_t prefix_len = strlen(prefix);
    if (strncmp(item, prefix, prefix_len) == 0) {
        return strdup(item + prefix_len);
    }
    return NULL;
}

int main() {
    // Test data mimicking repository headers and files
    char* items[] = {
        "Repository: serverGenesis",
        "file1.txt",
        "file2.c",
        "Repository: repoWatch",
        "main.c",
        "ui.c",
        "Repository: homeserver",
        "app.py",
        "config.json"
    };
    size_t item_count = 9;

    printf("Testing color assignment for %zu items:\n", item_count);
    for (size_t i = 0; i < item_count; i++) {
        printf("%zu: %s\n", i, items[i]);
    }
    printf("\n");

    // Simulate the FIXED color assignment logic
    int* item_colors = calloc(item_count, sizeof(int));
    int current_repo_color = 0;

    printf("Color assignment (FIXED - pre-assign to ALL items):\n");
    for (size_t i = 0; i < item_count; i++) {
        char* repo_name = extract_repo_name_from_header(items[i]);
        if (repo_name) {
            // Repository header - assign next alternating color
            current_repo_color++;
            if (current_repo_color > 8) current_repo_color = 1;
            item_colors[i] = current_repo_color;
            printf("Item %zu: Repository header '%s' -> Color %d\n", i, repo_name, item_colors[i]);
            free(repo_name);
        } else {
            // Content item - use the current repository's color
            item_colors[i] = current_repo_color;
            printf("Item %zu: Content item '%s' -> Color %d\n", i, items[i], item_colors[i]);
        }
    }

    printf("\nSimulating scrolling scenarios:\n");

    // Test different visible ranges (simulating scrolling)
    size_t scroll_positions[] = {0, 3, 6};
    size_t viewport_height = 4;

    for (size_t test = 0; test < 3; test++) {
        size_t start_item = scroll_positions[test];
        size_t end_item = start_item + viewport_height;
        if (end_item > item_count) end_item = item_count;

        printf("\nScroll position %zu (showing items %zu-%zu):\n", start_item, start_item, end_item - 1);

        for (size_t i = start_item; i < end_item; i++) {
            printf("  Item %zu: '%s' -> Color %d\n", i, items[i], item_colors[i]);
        }
    }

    free(item_colors);
    return 0;
}
