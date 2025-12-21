#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "../json-utils/json-utils.h"

// Style configuration for file colorization
typedef struct {
    int directory_color;
    int file_default_color;
    char** extensions;
    int* extension_colors;
    size_t extension_count;
    char** special_files;
    int* special_file_colors;
    size_t special_file_count;
} file_style_config_t;

// UI color configuration
typedef struct {
    int title_color;
    int header_separator_color;
    struct {
        int left;
        int center;
        int right;
    } pane_titles;
    struct {
        int vertical;
        int horizontal;
    } borders;
    struct {
        int separator;
        int text;
    } footer;
} ui_color_config_t;

// Complete style configuration
typedef struct {
    file_style_config_t files;
    ui_color_config_t ui;
} style_config_t;

// Configuration for three-pane-tui module
typedef struct {
    char* title;
    char* exit_keys;
    char* pane1_title;
    char* pane2_title;
    char* pane3_title;
    style_config_t styles;
} three_pane_tui_config_t;

// Hardcoded data for the three panes
typedef struct {
    char** pane1_items;
    size_t pane1_count;
    char** pane2_items;
    size_t pane2_count;
    char** pane3_items;
    size_t pane3_count;
} three_pane_data_t;

// Orchestrator for three-pane-tui module
typedef struct {
    char* module_path;
    three_pane_tui_config_t config;
    three_pane_data_t data;
} three_pane_tui_orchestrator_t;

// Global flag for redraw requests
volatile sig_atomic_t redraw_needed = 0;

// Signal handler for window resize
void handle_sigwinch(int sig) {
    redraw_needed = 1;
}

// Terminal control functions
void save_cursor_position() {
    printf("\033[s");
}

void restore_cursor_position() {
    printf("\033[u");
}

void hide_cursor() {
    printf("\033[?25l");
}

void show_cursor() {
    printf("\033[?25h");
}

void clear_screen() {
    printf("\033[2J");
}

void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

void reset_colors() {
    printf("\033[0m");
}

void set_bold() {
    printf("\033[1m");
}

void set_color(int color_code) {
    printf("\033[%dm", color_code);
}

void set_background(int color_code) {
    printf("\033[%dm", color_code + 10); // Background colors are 40-47, foreground are 30-37
}

// Get terminal size
int get_terminal_size(int* width, int* height) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        char* columns = getenv("COLUMNS");
        char* lines = getenv("LINES");

        if (columns && lines) {
            *width = atoi(columns);
            *height = atoi(lines);
            return 0;
        }

        *width = 80;
        *height = 24;
        return -1;
    }

    *width = ws.ws_col;
    *height = ws.ws_row;
    return 0;
}

// Simple environment variable expansion
char* expandvars(const char* input) {
    if (!input) return NULL;
    return strdup(input);
}

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

// Load style configuration from index.json
int load_styles(style_config_t* styles, const char* module_path) {
    fprintf(stderr, "DEBUG: About to parse index.json from module_path: %s\n", module_path);

    // Construct full path to index.json
    char index_path[2048];
    snprintf(index_path, sizeof(index_path), "%s/index.json", module_path);

    json_value_t* root = json_parse_file(index_path);
    fprintf(stderr, "DEBUG: Finished parsing %s\n", index_path);
    fprintf(stderr, "DEBUG: root = %p\n", root);
    if (root) {
        fprintf(stderr, "DEBUG: root type = %d (should be %d for JSON_OBJECT)\n", root->type, JSON_OBJECT);
        if (root->type == JSON_OBJECT) {
            fprintf(stderr, "DEBUG: root has %zu entries\n", root->value.obj_val->count);
            for (size_t i = 0; i < root->value.obj_val->count; i++) {
                fprintf(stderr, "DEBUG: key[%zu] = '%s'\n", i, root->value.obj_val->entries[i]->key);
            }
        }
    }
    if (!root || root->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load index.json\n");
        return -1;
    }

    // Get current scheme name
    json_value_t* current_scheme_val = get_nested_value(root, "styles.current_scheme");
    fprintf(stderr, "DEBUG: Looking for 'styles.current_scheme'\n");
    fprintf(stderr, "DEBUG: current_scheme_val = %p\n", current_scheme_val);
    if (current_scheme_val) {
        fprintf(stderr, "DEBUG: current_scheme_val type = %d (should be %d for JSON_STRING)\n", current_scheme_val->type, JSON_STRING);
        if (current_scheme_val->type == JSON_STRING) {
            fprintf(stderr, "DEBUG: current_scheme = '%s'\n", current_scheme_val->value.str_val);
        }
    } else {
        fprintf(stderr, "DEBUG: get_nested_value returned NULL\n");
    }
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

// Load configuration from index.json
int load_config(three_pane_tui_orchestrator_t* orch) {
    // Load JSON config
    json_value_t* config = json_parse_file("index.json");
    if (!config || config->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load config\n");
        return -1;
    }

    // Extract config values with defaults
    orch->config.title = expandvars("Three Pane TUI Demo");
    orch->config.exit_keys = strdup("qQ");
    orch->config.pane1_title = expandvars("Left Pane");
    orch->config.pane2_title = expandvars("Center Pane");
    orch->config.pane3_title = expandvars("Right Pane");

    // Load styles
    if (load_styles(&orch->config.styles, orch->module_path) != 0) {
        fprintf(stderr, "Failed to load styles\n");
        json_free(config);
        return -1;
    }

    json_free(config);
    return 0;
}

// Read and parse git-submodules.report for pane 1
int load_git_submodules_data(three_pane_tui_orchestrator_t* orch) {
    json_value_t* report = json_parse_file("git-submodules.report");
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load git-submodules.report\n");
        return -1;
    }

    // Get repositories array
    json_value_t* repos = get_nested_value(report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "No repositories found in report\n");
        json_free(report);
        return -1;
    }

    // Allocate space for repository data
    orch->data.pane1_count = repos->value.arr_val->count;
    orch->data.pane1_items = calloc(orch->data.pane1_count, sizeof(char*));

    // Parse each repository
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo = repos->value.arr_val->items[i];
        if (repo->type != JSON_OBJECT) continue;

        // Get repository name and status
        json_value_t* name = get_nested_value(repo, "name");
        json_value_t* status = get_nested_value(repo, "status");
        json_value_t* changes = get_nested_value(repo, "changes");

        // Format repository info
        char buffer[1024];
        if (name && name->type == JSON_STRING &&
            status && status->type == JSON_STRING) {
            if (changes && changes->type == JSON_STRING && strlen(changes->value.str_val) > 0) {
                // Show first line of changes if available
                char* newline_pos = strchr(changes->value.str_val, '\n');
                if (newline_pos) {
                    *newline_pos = '\0';
                }
                snprintf(buffer, sizeof(buffer), "%s [%s]: %s",
                        name->value.str_val, status->value.str_val, changes->value.str_val);
            } else {
                snprintf(buffer, sizeof(buffer), "%s [%s]",
                        name->value.str_val, status->value.str_val);
            }
        } else {
            snprintf(buffer, sizeof(buffer), "Unknown repo");
        }

        orch->data.pane1_items[i] = strdup(buffer);
    }

    json_free(report);
    return 0;
}

// Helper function to check if a filename is a known submodule
static int is_submodule(const char* filename, char** submodules, size_t submodule_count) {
    if (!filename || !submodules) return 0;
    for (size_t i = 0; i < submodule_count; i++) {
        if (strcmp(filename, submodules[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Read and parse committed-not-pushed-report.json for pane 2
int load_committed_not_pushed_data(three_pane_tui_orchestrator_t* orch) {
    // First, read git-submodules.report to get list of known submodules to filter out
    char** submodules = NULL;
    size_t submodule_count = 0;

    json_value_t* submodules_report = json_parse_file("git-submodules.report");
    if (submodules_report && submodules_report->type == JSON_OBJECT) {
        json_value_t* repos = get_nested_value(submodules_report, "repositories");
        if (repos && repos->type == JSON_ARRAY) {
            // Count non-root repositories (submodules)
            for (size_t i = 0; i < repos->value.arr_val->count; i++) {
                json_value_t* repo = repos->value.arr_val->items[i];
                if (repo->type != JSON_OBJECT) continue;
                json_value_t* name = get_nested_value(repo, "name");
                if (name && name->type == JSON_STRING && strcmp(name->value.str_val, "root") != 0) {
                    submodule_count++;
                }
            }

            // Allocate and populate submodules array
            submodules = calloc(submodule_count, sizeof(char*));
            size_t sub_idx = 0;
            for (size_t i = 0; i < repos->value.arr_val->count; i++) {
                json_value_t* repo = repos->value.arr_val->items[i];
                if (repo->type != JSON_OBJECT) continue;
                json_value_t* name = get_nested_value(repo, "name");
                if (name && name->type == JSON_STRING && strcmp(name->value.str_val, "root") != 0) {
                    submodules[sub_idx++] = strdup(name->value.str_val);
                }
            }
        }
    }

    json_value_t* report = json_parse_file("committed-not-pushed-report.json");
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Error: Cannot parse committed-not-pushed-report.json\n");
        if (submodules_report) json_free(submodules_report);
        return 1;
    }

    json_value_t* repos = get_nested_value(report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "Error: No repositories array in committed-not-pushed-report.json\n");
        json_free(report);
        if (submodules_report) json_free(submodules_report);
        return 1;
    }

    // Count total items needed (repository headers + commits + non-submodule files)
    size_t total_items = 0;
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo = repos->value.arr_val->items[i];
        if (repo->type != JSON_OBJECT) continue;

        json_value_t* commits = get_nested_value(repo, "unpushed_commits");
        if (commits && commits->type == JSON_ARRAY) {
            total_items += 1; // Repository header
            for (size_t j = 0; j < commits->value.arr_val->count; j++) {
                json_value_t* commit = commits->value.arr_val->items[j];
                if (commit->type != JSON_OBJECT) continue;
                total_items += 1; // Commit info
                json_value_t* files = get_nested_value(commit, "files_changed");
                if (files && files->type == JSON_ARRAY) {
                    // Count only non-submodule files
                    for (size_t k = 0; k < files->value.arr_val->count; k++) {
                        json_value_t* file = files->value.arr_val->items[k];
                        if (file->type == JSON_STRING && !is_submodule(file->value.str_val, submodules, submodule_count)) {
                            total_items++;
                        }
                    }
                }
            }
        }
    }

    orch->data.pane2_count = total_items;
    orch->data.pane2_items = calloc(total_items, sizeof(char*));

    // Parse committed not pushed data from each repository
    size_t item_index = 0;
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo = repos->value.arr_val->items[i];
        if (repo->type != JSON_OBJECT) continue;

        json_value_t* repo_name = get_nested_value(repo, "name");
        json_value_t* commits = get_nested_value(repo, "unpushed_commits");

        if (!repo_name || repo_name->type != JSON_STRING) continue;
        if (!commits || commits->type != JSON_ARRAY) continue;

        // Add repository header - use actual repo name from path if available
        json_value_t* repo_path = get_nested_value(repo, "path");
        char header_buffer[512];
        if (repo_path && repo_path->type == JSON_STRING) {
            // Extract repo name from path (last component after '/')
            const char* path = repo_path->value.str_val;
            const char* repo_name_from_path = strrchr(path, '/');
            if (repo_name_from_path) {
                repo_name_from_path++; // Skip the '/'
                snprintf(header_buffer, sizeof(header_buffer), "Repository: %s", repo_name_from_path);
            } else {
                snprintf(header_buffer, sizeof(header_buffer), "Repository: %s", repo_name->value.str_val);
            }
        } else {
            snprintf(header_buffer, sizeof(header_buffer), "Repository: %s", repo_name->value.str_val);
        }
        orch->data.pane2_items[item_index++] = strdup(header_buffer);

        // Add each commit and its files
        for (size_t j = 0; j < commits->value.arr_val->count; j++) {
            json_value_t* commit = commits->value.arr_val->items[j];
            if (commit->type != JSON_OBJECT) continue;

            json_value_t* commit_info = get_nested_value(commit, "commit_info");
            json_value_t* files_changed = get_nested_value(commit, "files_changed");

            // Add commit info
            if (commit_info && commit_info->type == JSON_STRING) {
                char commit_buffer[1024];
                // Truncate commit info if too long
                const char* info = commit_info->value.str_val;
                if (strlen(info) > 60) {
                    snprintf(commit_buffer, sizeof(commit_buffer), "└── %.60s...", info);
                } else {
                    snprintf(commit_buffer, sizeof(commit_buffer), "└── %s", info);
                }
                orch->data.pane2_items[item_index++] = strdup(commit_buffer);
            }

            // Add files changed (skip submodules)
            if (files_changed && files_changed->type == JSON_ARRAY) {
                for (size_t k = 0; k < files_changed->value.arr_val->count; k++) {
                    json_value_t* file = files_changed->value.arr_val->items[k];
                    if (file->type == JSON_STRING && !is_submodule(file->value.str_val, submodules, submodule_count)) {
                        char file_buffer[1024];
                        snprintf(file_buffer, sizeof(file_buffer), "    ├── %s", file->value.str_val);
                        orch->data.pane2_items[item_index++] = strdup(file_buffer);
                    }
                }
            }
        }
    }

    json_free(report);
    if (submodules_report) json_free(submodules_report);

    // Cleanup submodules array
    if (submodules) {
        for (size_t i = 0; i < submodule_count; i++) {
            free(submodules[i]);
        }
        free(submodules);
    }

    return 0;
}

// Read and parse dirty-files-report.json for pane 2
int load_dirty_files_data(three_pane_tui_orchestrator_t* orch) {
    json_value_t* report = json_parse_file("dirty-files-report.json");
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load dirty-files-report.json\n");
        return -1;
    }

    // Get repositories array
    json_value_t* repos = get_nested_value(report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "No repositories found in dirty-files report\n");
        json_free(report);
        return -1;
    }

    // Count total dirty files across all repositories
    size_t total_files = 0;
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo = repos->value.arr_val->items[i];
        if (repo->type != JSON_OBJECT) continue;

        json_value_t* files = get_nested_value(repo, "dirty_files");
        if (files && files->type == JSON_ARRAY) {
            total_files += files->value.arr_val->count;
        }
    }

    // Allocate space for all dirty files
    orch->data.pane2_count = total_files;
    orch->data.pane2_items = calloc(total_files, sizeof(char*));

    // Parse dirty files from each repository
    size_t file_index = 0;
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo = repos->value.arr_val->items[i];
        if (repo->type != JSON_OBJECT) continue;

        json_value_t* repo_name = get_nested_value(repo, "name");
        json_value_t* files = get_nested_value(repo, "dirty_files");

        if (!files || files->type != JSON_ARRAY) continue;

        // Add each dirty file with repository prefix
        for (size_t j = 0; j < files->value.arr_val->count && file_index < total_files; j++) {
            json_value_t* file = files->value.arr_val->items[j];
            if (file->type == JSON_STRING) {
                char buffer[1024];
                if (repo_name && repo_name->type == JSON_STRING) {
                    snprintf(buffer, sizeof(buffer), "%s: %s",
                            repo_name->value.str_val, file->value.str_val);
                } else {
                    snprintf(buffer, sizeof(buffer), "%s", file->value.str_val);
                }
                orch->data.pane2_items[file_index] = strdup(buffer);
                file_index++;
            }
        }
    }

    json_free(report);
    return 0;
}

// Load hardcoded data for the third pane (right pane)
int load_hardcoded_data(three_pane_tui_orchestrator_t* orch) {
    // Right pane data (keeping this hardcoded as requested)
    orch->data.pane3_count = 6;
    orch->data.pane3_items = calloc(6, sizeof(char*));
    orch->data.pane3_items[0] = strdup("Right 1");
    orch->data.pane3_items[1] = strdup("Right 2");
    orch->data.pane3_items[2] = strdup("Right 3");
    orch->data.pane3_items[3] = strdup("Right 4");
    orch->data.pane3_items[4] = strdup("Right 5");
    orch->data.pane3_items[5] = strdup("Right 6");

    return 0;
}

// Initialize orchestrator
three_pane_tui_orchestrator_t* three_pane_tui_init(const char* module_path) {
    three_pane_tui_orchestrator_t* orch = calloc(1, sizeof(three_pane_tui_orchestrator_t));
    if (!orch) return NULL;

    orch->module_path = strdup(module_path);
    if (!orch->module_path) {
        free(orch);
        return NULL;
    }

    if (load_config(orch) != 0) {
        free(orch->module_path);
        free(orch);
        return NULL;
    }

    // Load hardcoded data for pane 1 (left pane)
    orch->data.pane1_count = 1;
    orch->data.pane1_items = calloc(1, sizeof(char*));
    orch->data.pane1_items[0] = strdup("n00dles");

    if (load_committed_not_pushed_data(orch) != 0) {
        fprintf(stderr, "Warning: Failed to load committed-not-pushed data, using fallback\n");
        // Could add fallback data here if needed
    }

    if (load_hardcoded_data(orch) != 0) {
        free(orch->config.title);
        free(orch->config.exit_keys);
        free(orch->config.pane1_title);
        free(orch->config.pane2_title);
        free(orch->config.pane3_title);
        free(orch->module_path);
        free(orch);
        return NULL;
    }

    return orch;
}

// Cleanup orchestrator
void three_pane_tui_cleanup(three_pane_tui_orchestrator_t* orch) {
    if (orch) {
        // Cleanup config
        free(orch->config.title);
        free(orch->config.exit_keys);
        free(orch->config.pane1_title);
        free(orch->config.pane2_title);
        free(orch->config.pane3_title);

        // Cleanup data
        for (size_t i = 0; i < orch->data.pane1_count; i++) {
            free(orch->data.pane1_items[i]);
        }
        free(orch->data.pane1_items);

        for (size_t i = 0; i < orch->data.pane2_count; i++) {
            free(orch->data.pane2_items[i]);
        }
        free(orch->data.pane2_items);

        for (size_t i = 0; i < orch->data.pane3_count; i++) {
            free(orch->data.pane3_items[i]);
        }
        free(orch->data.pane3_items);

        free(orch->module_path);
        free(orch);
    }
}

// Draw a single pane
void draw_pane(int start_col, int width, int height, const char* title, char** items, size_t item_count, int title_color, const style_config_t* styles, int pane_index) {
    // Draw title at the top of the pane (row 3, since row 1 is main title, row 2 is header separator)
    move_cursor(3, start_col);
    set_color(title_color);
    set_bold();
    printf("%s", title);
    reset_colors();

    // Draw items starting from row 4 (right after pane title and header separator)
    // height parameter is the available rows for content (from row 4 onwards)
    int current_row = 4;
    int max_row = 3 + height; // height is available content rows, so max is row 3 + height
    for (size_t i = 0; i < item_count && current_row <= max_row; i++) {
        move_cursor(current_row, start_col);
        // Apply color based on file type
        int file_color = get_file_color(items[i], styles);
        set_color(file_color);

        // Truncate text to fit within pane width (leave room for potential border)
        const char* text = items[i];
        int max_text_width = width - 2; // Leave some margin
        if (strlen(text) > max_text_width) {
            // Truncate and add ellipsis
            char truncated[1024];
            snprintf(truncated, sizeof(truncated), "%.*s...", max_text_width - 3, text);
            printf("%s", truncated);
        } else {
            printf("%s", text);
        }
        reset_colors();
        current_row++;
    }
}

// Draw the three-pane TUI overlay
void draw_tui_overlay(three_pane_tui_orchestrator_t* orch) {
    int width, height;
    get_terminal_size(&width, &height);

    clear_screen();

    // Main title at the very top
    move_cursor(1, 1);
    set_color(orch->config.styles.ui.title_color);
    set_bold();
    printf("%s", orch->config.title);
    reset_colors();

    // Horizontal line under the header
    move_cursor(2, 1);
    set_color(orch->config.styles.ui.header_separator_color);
    for (int i = 0; i < width; i++) {
        printf("─");
    }
    reset_colors();

    // Calculate pane dimensions to maximize screen real estate
    int pane_width = width / 3;
    int remaining_width = width % 3; // Handle case where width is not divisible by 3
    int pane_height = height - 4; // Available rows: total height minus main title row, header separator, pane title row, and footer separator

    // Draw vertical border lines between panes
    set_color(orch->config.styles.ui.borders.vertical);
    for (int row = 3; row <= height - 2; row++) { // From row 3 to row before footer separator
        // Border between pane 1 and 2
        move_cursor(row, pane_width);
        printf("│");

        // Border between pane 2 and 3
        move_cursor(row, pane_width * 2);
        printf("│");
    }
    reset_colors();

    // Horizontal line above the footer
    move_cursor(height - 1, 1);
    set_color(32); // Green for footer separator
    for (int i = 0; i < width; i++) {
        printf("─");
    }
    reset_colors();

    // Draw three panes side by side, maximizing screen space
    // Each pane starts at row 2 (below the main title)
    draw_pane(1, pane_width - 1, pane_height, orch->config.pane1_title,
              orch->data.pane1_items, orch->data.pane1_count, orch->config.styles.ui.pane_titles.left, &orch->config.styles);

    draw_pane(pane_width + 1, pane_width - 1, pane_height, orch->config.pane2_title,
              orch->data.pane2_items, orch->data.pane2_count, orch->config.styles.ui.pane_titles.center, &orch->config.styles);

    // Rightmost pane gets any remaining width minus the border
    draw_pane(pane_width * 2 + 1, pane_width + remaining_width - 1, pane_height, orch->config.pane3_title,
              orch->data.pane3_items, orch->data.pane3_count, orch->config.styles.ui.pane_titles.right, &orch->config.styles);

    // Footer at bottom (after the horizontal separator)
    move_cursor(height, 1);
    set_color(32); // Green for footer text
    printf("Q: exit - Three Pane TUI Demo");
    reset_colors();

    fflush(stdout);
}

// Execute the three-pane-tui module
int three_pane_tui_execute(three_pane_tui_orchestrator_t* orch) {
    // Set up signal handler for window resize
    struct sigaction sa;
    sa.sa_handler = handle_sigwinch;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    // Save current terminal state
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;

    // Disable canonical mode and echo for immediate key input
    new_tio.c_lflag &= (~ICANON & ~ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    // Make stdin non-blocking
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    // Hide cursor and save position
    hide_cursor();
    save_cursor_position();

    // Draw the initial TUI overlay
    draw_tui_overlay(orch);

    // Main input loop
    int running = 1;

    while (running) {
        // Check for redraw request from signal handler
        if (redraw_needed) {
            redraw_needed = 0;
            draw_tui_overlay(orch);
        }

        // Check for keyboard input (non-blocking)
        int c = getchar();
        if (c != EOF) {
            // Check for exit keys
            if (c == 'q' || c == 'Q' || c == 27) { // 27 is Escape
                running = 0;
            }
        }

        // Small delay to prevent busy waiting
        usleep(10000); // 10ms
    }

    // Cleanup: restore terminal state
    clear_screen();
    restore_cursor_position();
    show_cursor();

    // Restore blocking mode
    fcntl(STDIN_FILENO, F_SETFL, flags);

    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

    return 0;
}

int main(int argc, char* argv[]) {
    // Get the module path - three-pane-tui is executed from repoWatch root,
    // but its files are in the three-pane-tui subdirectory
    char module_path[1024];
    if (!getcwd(module_path, sizeof(module_path))) {
        fprintf(stderr, "Error: Cannot get current working directory\n");
        return 1;
    }

    // Append the three-pane-tui subdirectory to the module path
    char full_module_path[2048];
    snprintf(full_module_path, sizeof(full_module_path), "%s/three-pane-tui", module_path);
    fprintf(stderr, "DEBUG: module_path = '%s'\n", module_path);
    fprintf(stderr, "DEBUG: full_module_path = '%s'\n", full_module_path);

    // Initialize three-pane-tui orchestrator
    three_pane_tui_orchestrator_t* orch = three_pane_tui_init(full_module_path);
    if (!orch) {
        fprintf(stderr, "Error: Failed to initialize three-pane-tui orchestrator\n");
        return 1;
    }

    // Execute three-pane-tui module
    int result = three_pane_tui_execute(orch);

    // Cleanup
    three_pane_tui_cleanup(orch);

    return result;
}
