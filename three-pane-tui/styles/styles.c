#include "../three-pane-tui.h"

// Get color for a file based on its path
int get_file_color(const char* filepath, const style_config_t* styles) {
    // Check if it's a directory (ends with /)
    if (filepath[strlen(filepath) - 1] == '/') {
        return styles->files.directory_color;
    }

    // Extract filename for special file checking
    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    // Check special files first
    for (size_t i = 0; i < styles->files.special_file_count; i++) {
        if (strcmp(filename, styles->files.special_files[i]) == 0) {
            return styles->files.special_file_colors[i];
        }
    }

    // Check extensions
    const char* ext = strrchr(filename, '.');
    if (ext) {
        for (size_t i = 0; i < styles->files.extension_count; i++) {
            if (strcmp(ext, styles->files.extensions[i]) == 0) {
                return styles->files.extension_colors[i];
            }
        }
    }

    // Default file color
    return styles->files.file_default_color;
}

// Get deterministic color index (1-8) for a repository based on its name
int get_repo_color_index(const char* repo_name) {
    if (!repo_name) return 7; // Default white (index 7)

    // Simple hash function for deterministic color assignment (djb2 algorithm)
    unsigned long hash = 5381;
    const char* p = repo_name;
    while (*p) {
        hash = ((hash << 5) + hash) + *p;
        p++;
    }

    // Return color index 1-8 (instead of ANSI codes)
    return (hash % 8) + 1;
}

// Convert color index (1-8) to ANSI color code
int color_index_to_ansi(int index) {
    // 8-color palette: red, green, yellow, blue, magenta, cyan, white, bright green
    int colors[] = {31, 32, 33, 34, 35, 36, 37, 92};
    if (index >= 1 && index <= 8) {
        return colors[index - 1]; // Convert 1-based index to 0-based array
    }
    return 37; // Default white for invalid indices
}

// Adjust color indices to ensure no two adjacent items have the same color
// Modifies the colors array in-place
void adjust_colors_no_touching(int* colors, size_t count) {
    if (!colors || count <= 1) return;

    for (size_t i = 1; i < count; i++) {
        if (colors[i] == colors[i - 1]) {
            // Increment color and wrap around (8 -> 1)
            colors[i] = (colors[i] % 8) + 1;
        }
    }
}

// Get deterministic ANSI color for a repository based on its name (backwards compatibility)
int get_repo_color(const char* repo_name) {
    int index = get_repo_color_index(repo_name);
    return color_index_to_ansi(index);
}

// Load style configuration from index.json
int load_styles(style_config_t* styles, const char* module_path) {
    // Construct full path to three-pane-tui/styles/index.json
    char index_path[2048];
    snprintf(index_path, sizeof(index_path), "%s/three-pane-tui/styles/index.json", module_path);

    json_value_t* root = json_parse_file(index_path);
    if (!root || root->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load index.json\n");
        return -1;
    }

    // Get current scheme name
    json_value_t* current_scheme_val = get_nested_value(root, "styles.current_scheme");
    if (!current_scheme_val || current_scheme_val->type != JSON_STRING) {
        fprintf(stderr, "No current_scheme found in styles\n");
        json_free(root);
        return -1;
    }
    const char* current_scheme = current_scheme_val->value.str_val;

    // Build path to current scheme
    char scheme_path[256];
    snprintf(scheme_path, sizeof(scheme_path), "styles.color_schemes.%s", current_scheme);

    // Get scheme configuration
    json_value_t* scheme_config = get_nested_value(root, scheme_path);
    if (!scheme_config || scheme_config->type != JSON_OBJECT) {
        fprintf(stderr, "Color scheme '%s' not found\n", current_scheme);
        json_free(root);
        return -1;
    }

    // Parse file styling using nested value access
    json_value_t* dir_color = get_nested_value(scheme_config, "directory");
    if (dir_color && dir_color->type == JSON_NUMBER) {
        styles->files.directory_color = (int)dir_color->value.num_val;
    }

    json_value_t* default_color = get_nested_value(scheme_config, "file_default");
    if (default_color && default_color->type == JSON_NUMBER) {
        styles->files.file_default_color = (int)default_color->value.num_val;
    }

    // Parse extensions
    json_value_t* extensions = get_nested_value(scheme_config, "extensions");
    if (extensions && extensions->type == JSON_OBJECT) {
        styles->files.extension_count = extensions->value.obj_val->count;
        styles->files.extensions = calloc(styles->files.extension_count, sizeof(char*));
        styles->files.extension_colors = calloc(styles->files.extension_count, sizeof(int));

        for (size_t j = 0; j < extensions->value.obj_val->count; j++) {
            json_entry_t* ext_entry = extensions->value.obj_val->entries[j];
            styles->files.extensions[j] = strdup(ext_entry->key);
            if (ext_entry->value->type == JSON_NUMBER) {
                styles->files.extension_colors[j] = (int)ext_entry->value->value.num_val;
            }
        }
    }

    // Parse special files
    json_value_t* special_files = get_nested_value(scheme_config, "special_files");
    if (special_files && special_files->type == JSON_OBJECT) {
        styles->files.special_file_count = special_files->value.obj_val->count;
        styles->files.special_files = calloc(styles->files.special_file_count, sizeof(char*));
        styles->files.special_file_colors = calloc(styles->files.special_file_count, sizeof(int));

        for (size_t j = 0; j < special_files->value.obj_val->count; j++) {
            json_entry_t* file_entry = special_files->value.obj_val->entries[j];
            styles->files.special_files[j] = strdup(file_entry->key);
            if (file_entry->value->type == JSON_NUMBER) {
                styles->files.special_file_colors[j] = (int)file_entry->value->value.num_val;
            }
        }
    }

    // Parse UI colors using nested access
    json_value_t* title_color = get_nested_value(root, "styles.ui_colors.title");
    if (title_color && title_color->type == JSON_NUMBER) {
        styles->ui.title_color = (int)title_color->value.num_val;
    }

    json_value_t* header_sep_color = get_nested_value(root, "styles.ui_colors.header_separator");
    if (header_sep_color && header_sep_color->type == JSON_NUMBER) {
        styles->ui.header_separator_color = (int)header_sep_color->value.num_val;
    }

    // Pane title colors
    json_value_t* left_title = get_nested_value(root, "styles.ui_colors.pane_titles.left");
    if (left_title && left_title->type == JSON_NUMBER) {
        styles->ui.pane_titles.left = (int)left_title->value.num_val;
    }

    json_value_t* center_title = get_nested_value(root, "styles.ui_colors.pane_titles.center");
    if (center_title && center_title->type == JSON_NUMBER) {
        styles->ui.pane_titles.center = (int)center_title->value.num_val;
    }

    json_value_t* right_title = get_nested_value(root, "styles.ui_colors.pane_titles.right");
    if (right_title && right_title->type == JSON_NUMBER) {
        styles->ui.pane_titles.right = (int)right_title->value.num_val;
    }

    // Border colors
    json_value_t* vert_border = get_nested_value(root, "styles.ui_colors.borders.vertical");
    if (vert_border && vert_border->type == JSON_NUMBER) {
        styles->ui.borders.vertical = (int)vert_border->value.num_val;
    }

    json_value_t* horiz_border = get_nested_value(root, "styles.ui_colors.borders.horizontal");
    if (horiz_border && horiz_border->type == JSON_NUMBER) {
        styles->ui.borders.horizontal = (int)horiz_border->value.num_val;
    }

    // Footer colors
    json_value_t* footer_sep = get_nested_value(root, "styles.ui_colors.footer.separator");
    if (footer_sep && footer_sep->type == JSON_NUMBER) {
        styles->ui.footer.separator = (int)footer_sep->value.num_val;
    }

    json_value_t* footer_text = get_nested_value(root, "styles.ui_colors.footer.text");
    if (footer_text && footer_text->type == JSON_NUMBER) {
        styles->ui.footer.text = (int)footer_text->value.num_val;
    }

    json_free(root);
    return 0;
}
