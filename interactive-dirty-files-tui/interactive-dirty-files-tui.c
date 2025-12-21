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

// Tree node structure for hierarchical display
typedef struct tree_node {
    char* name;
    struct tree_node** children;
    size_t child_count;
    int is_file;
} tree_node_t;

// View mode enumeration
typedef enum {
    VIEW_FLAT,
    VIEW_TREE
} view_mode_t;

// Structure for dirty repository data
typedef struct {
    char* name;
    char* path;
    int dirty_file_count;
    char** dirty_files;
    size_t file_count;
    tree_node_t* file_tree;
} dirty_repo_t;

// Structure for dirty files report data
typedef struct {
    char* report_type;
    char* generated_by;
    time_t timestamp;
    dirty_repo_t* repositories;
    size_t repo_count;
    int total_dirty_repositories;
    int total_dirty_files;
} dirty_files_report_t;

// Configuration for interactive-dirty-files-tui module
typedef struct {
    char* title;
    char* exit_keys;
    char* refresh_keys;
    char* toggle_keys;
    int refresh_interval;
    int max_display_files;
    char* report_file;
    view_mode_t default_view;
    char* tree_prefix;
    char* tree_last_prefix;
    char* tree_indent;
} interactive_dirty_files_tui_config_t;

// Global flag for redraw requests
volatile sig_atomic_t redraw_needed = 0;

// Signal handler for window resize
void handle_sigwinch(int sig) {
    redraw_needed = 1;
}

// Orchestrator for interactive-dirty-files-tui module
typedef struct {
    char* module_path;
    interactive_dirty_files_tui_config_t config;
    dirty_files_report_t report;
    view_mode_t current_view;
} interactive_dirty_files_tui_orchestrator_t;

// Cleanup tree node recursively
void cleanup_tree_node(tree_node_t* node) {
    if (!node) return;

    for (size_t i = 0; i < node->child_count; i++) {
        cleanup_tree_node(node->children[i]);
    }

    free(node->children);
    free(node->name);
    free(node);
}

// Build file tree from flat file paths
tree_node_t* build_file_tree(char** files, size_t file_count) {
    tree_node_t* root = calloc(1, sizeof(tree_node_t));
    if (!root) return NULL;

    root->name = strdup("/");
    root->is_file = 0;

    for (size_t i = 0; i < file_count; i++) {
        const char* path = files[i];
        tree_node_t* current = root;

        // Skip leading slash
        if (path[0] == '/') path++;

        char* path_copy = strdup(path);
        char* token = strtok(path_copy, "/");

        while (token) {
            // Check if child already exists
            tree_node_t* child = NULL;
            for (size_t j = 0; j < current->child_count; j++) {
                if (strcmp(current->children[j]->name, token) == 0) {
                    child = current->children[j];
                    break;
                }
            }

            // Create new child if not found
            if (!child) {
                child = calloc(1, sizeof(tree_node_t));
                child->name = strdup(token);

                // Check if this is the last token (file) or not (directory)
                char* next_token = strtok(NULL, "/");
                child->is_file = (next_token == NULL);

                // Put token back for proper processing
                if (next_token) {
                    // This is a directory, put the token back
                    char* temp = strtok(NULL, ""); // Get rest of string
                    if (temp) {
                        char* full_rest = malloc(strlen(next_token) + strlen(temp) + 2);
                        sprintf(full_rest, "%s/%s", next_token, temp);
                        strtok(full_rest, "/"); // Reset strtok state
                        free(full_rest);
                    }
                }

                // Add to parent's children
                current->children = realloc(current->children, (current->child_count + 1) * sizeof(tree_node_t*));
                current->children[current->child_count] = child;
                current->child_count++;
            }

            current = child;

            // Get next token only if we haven't already processed it
            if (!child->is_file) {
                token = strtok(NULL, "/");
            } else {
                token = NULL;
            }
        }

        free(path_copy);
    }

    return root;
}

// Print tree node with proper indentation
void print_tree_node(tree_node_t* node, int depth, int is_last, const char* prefix, const char* last_prefix, const char* indent, int max_width, int current_row, int max_row) {
    if (current_row >= max_row) return;

    // Print indentation
    for (int i = 0; i < depth; i++) {
        printf("%s", indent);
    }

    // Print tree prefix
    if (depth > 0) {
        if (is_last) {
            printf("%s", last_prefix);
        } else {
            printf("%s", prefix);
        }
    }

    // Print node name (truncated if necessary)
    char truncated_name[256];
    size_t len = strlen(node->name);
    if (len <= max_width) {
        strcpy(truncated_name, node->name);
    } else {
        int copy_len = max_width - 3;
        if (copy_len < 1) copy_len = 1;
        strncpy(truncated_name, node->name, copy_len);
        truncated_name[copy_len] = '\0';
        strcat(truncated_name, "...");
    }

    printf("%s\n", truncated_name);
    current_row++;

    // Print children
    for (size_t i = 0; i < node->child_count; i++) {
        int child_is_last = (i == node->child_count - 1);
        print_tree_node(node->children[i], depth + 1, child_is_last, prefix, last_prefix, indent, max_width, current_row, max_row);
    }
}

// Cleanup report data
void cleanup_report(dirty_files_report_t* report) {
    free(report->report_type);
    free(report->generated_by);

    for (size_t i = 0; i < report->repo_count; i++) {
        free(report->repositories[i].name);
        free(report->repositories[i].path);
        for (size_t j = 0; j < report->repositories[i].file_count; j++) {
            free(report->repositories[i].dirty_files[j]);
        }
        free(report->repositories[i].dirty_files);
        cleanup_tree_node(report->repositories[i].file_tree);
    }
    free(report->repositories);
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

// Load configuration from index.json
int load_config(interactive_dirty_files_tui_orchestrator_t* orch) {
    // Load JSON config
    json_value_t* config = json_parse_file("index.json");
    if (!config || config->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load config\n");
        return -1;
    }

    // Extract config values
    orch->config.title = expandvars("Interactive Dirty Files Analysis");
    orch->config.exit_keys = strdup("qQ");
    orch->config.refresh_keys = strdup("rR");
    orch->config.toggle_keys = strdup(" ");
    orch->config.refresh_interval = 5000;
    orch->config.max_display_files = 50;
    orch->config.report_file = expandvars("dirty-files-report.json");
    orch->config.default_view = VIEW_FLAT;
    orch->config.tree_prefix = strdup("├── ");
    orch->config.tree_last_prefix = strdup("└── ");
    orch->config.tree_indent = strdup("│   ");

    // Set current view to default
    orch->current_view = orch->config.default_view;

    json_free(config);
    return 0;
}

// Load dirty files report from JSON
int load_dirty_files_report(interactive_dirty_files_tui_orchestrator_t* orch) {
    // Clean up previous report
    cleanup_report(&orch->report);

    // Load JSON report
    json_value_t* root = json_parse_file(orch->config.report_file);
    if (!root || root->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load dirty files report\n");
        return -1;
    }

    // Parse metadata
    for (size_t i = 0; i < root->value.obj_val->count; i++) {
        json_entry_t* entry = root->value.obj_val->entries[i];

        if (strcmp(entry->key, "report_type") == 0 && entry->value->type == JSON_STRING) {
            orch->report.report_type = strdup(entry->value->value.str_val);
        } else if (strcmp(entry->key, "generated_by") == 0 && entry->value->type == JSON_STRING) {
            orch->report.generated_by = strdup(entry->value->value.str_val);
        } else if (strcmp(entry->key, "timestamp") == 0 && entry->value->type == JSON_NUMBER) {
            orch->report.timestamp = (time_t)entry->value->value.num_val;
        } else if (strcmp(entry->key, "repositories") == 0 && entry->value->type == JSON_ARRAY) {
            // Parse repositories array
            json_array_t* repos_arr = entry->value->value.arr_val;
            orch->report.repo_count = repos_arr->count;
            orch->report.repositories = calloc(repos_arr->count, sizeof(dirty_repo_t));

            for (size_t j = 0; j < repos_arr->count; j++) {
                json_value_t* repo_obj = repos_arr->items[j];
                if (repo_obj->type != JSON_OBJECT) continue;

                dirty_repo_t* repo = &orch->report.repositories[j];

                for (size_t k = 0; k < repo_obj->value.obj_val->count; k++) {
                    json_entry_t* repo_entry = repo_obj->value.obj_val->entries[k];

                    if (strcmp(repo_entry->key, "name") == 0 && repo_entry->value->type == JSON_STRING) {
                        repo->name = strdup(repo_entry->value->value.str_val);
                    } else if (strcmp(repo_entry->key, "path") == 0 && repo_entry->value->type == JSON_STRING) {
                        repo->path = strdup(repo_entry->value->value.str_val);
                    } else if (strcmp(repo_entry->key, "dirty_file_count") == 0 && repo_entry->value->type == JSON_NUMBER) {
                        repo->dirty_file_count = (int)repo_entry->value->value.num_val;
                    } else if (strcmp(repo_entry->key, "dirty_files") == 0 && repo_entry->value->type == JSON_ARRAY) {
                        json_array_t* files_arr = repo_entry->value->value.arr_val;
                        repo->file_count = files_arr->count;
                        repo->dirty_files = calloc(files_arr->count, sizeof(char*));

                        for (size_t l = 0; l < files_arr->count; l++) {
                            if (files_arr->items[l]->type == JSON_STRING) {
                                repo->dirty_files[l] = strdup(files_arr->items[l]->value.str_val);
                            }
                        }

                        // Build file tree for this repository
                        repo->file_tree = build_file_tree(repo->dirty_files, repo->file_count);
                    }
                }
            }
        } else if (strcmp(entry->key, "summary") == 0 && entry->value->type == JSON_OBJECT) {
            // Parse summary
            json_object_t* summary_obj = entry->value->value.obj_val;
            for (size_t j = 0; j < summary_obj->count; j++) {
                json_entry_t* sum_entry = summary_obj->entries[j];

                if (strcmp(sum_entry->key, "total_dirty_repositories") == 0 && sum_entry->value->type == JSON_NUMBER) {
                    orch->report.total_dirty_repositories = (int)sum_entry->value->value.num_val;
                } else if (strcmp(sum_entry->key, "total_dirty_files") == 0 && sum_entry->value->type == JSON_NUMBER) {
                    orch->report.total_dirty_files = (int)sum_entry->value->value.num_val;
                }
            }
        }
    }

    json_free(root);
    return 0;
}

// Initialize orchestrator
interactive_dirty_files_tui_orchestrator_t* interactive_dirty_files_tui_init(const char* module_path) {
    interactive_dirty_files_tui_orchestrator_t* orch = calloc(1, sizeof(interactive_dirty_files_tui_orchestrator_t));
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

    // Initialize report
    memset(&orch->report, 0, sizeof(dirty_files_report_t));

    return orch;
}

// Cleanup orchestrator
void interactive_dirty_files_tui_cleanup(interactive_dirty_files_tui_orchestrator_t* orch) {
    if (orch) {
        cleanup_report(&orch->report);
        free(orch->config.title);
        free(orch->config.exit_keys);
        free(orch->config.refresh_keys);
        free(orch->config.toggle_keys);
        free(orch->config.report_file);
        free(orch->config.tree_prefix);
        free(orch->config.tree_last_prefix);
        free(orch->config.tree_indent);
        free(orch->module_path);
        free(orch);
    }
}

// Extract just the filename from a path
const char* extract_filename(const char* filepath) {
    const char* last_slash = strrchr(filepath, '/');
    return last_slash ? last_slash + 1 : filepath;
}

// Get a better repository name (extract from path if name is generic)
const char* get_display_repo_name(const char* repo_name, const char* repo_path) {
    // If the repo name is "root", try to get a better name from the path
    if (strcmp(repo_name, "root") == 0) {
        const char* last_slash = strrchr(repo_path, '/');
        if (last_slash && strlen(last_slash + 1) > 0) {
            return last_slash + 1;
        }
    }
    return repo_name;
}

// Truncate filename with ellipsis if too long
void truncate_filename(const char* filename, char* buffer, size_t buffer_size, int max_width) {
    size_t len = strlen(filename);
    if (len <= max_width) {
        strcpy(buffer, filename);
        return;
    }

    // Reserve space for "..."
    int ellipsis_len = 3;
    int copy_len = max_width - ellipsis_len;
    if (copy_len < 1) copy_len = 1;

    strncpy(buffer, filename, copy_len);
    buffer[copy_len] = '\0';
    strcat(buffer, "...");
}

// Draw flat view (like original dirty-files-tui)
void draw_flat_view(interactive_dirty_files_tui_orchestrator_t* orch, int width, int height) {
    int current_row = 1;

    // Title at top (left-aligned, no padding)
    set_color(36); // Cyan color
    set_bold();
    printf("%s (FLAT)\n", orch->config.title);
    current_row++;

    // Summary line (compact)
    if (current_row < height - 1) {
        reset_colors();
        printf("Total: %d dirty repos, %d dirty files\n",
               orch->report.total_dirty_repositories, orch->report.total_dirty_files);
        current_row++;
    }

    reset_colors();

    // Display files grouped by repository with headers
    for (size_t i = 0; i < orch->report.repo_count && current_row < height - 1; i++) {
        dirty_repo_t* repo = &orch->report.repositories[i];

        // Calculate maximum content width for this repository
        int max_content_width = 0;
        for (size_t j = 0; j < repo->file_count; j++) {
            const char* filename = extract_filename(repo->dirty_files[j]);
            int filename_len = strlen(filename);
            if (filename_len > max_content_width) {
                max_content_width = filename_len;
            }
        }

        // Repository header (centered over content)
        if (current_row < height - 1) {
            set_color(36); // Cyan color
            set_bold();

            const char* display_name = get_display_repo_name(repo->name, repo->path);
            char header[256];
            snprintf(header, sizeof(header), "Repository: %s", display_name);
            int header_len = strlen(header);
            int content_width = (max_content_width > header_len) ? max_content_width : header_len;
            int padding = (width - content_width) / 2;
            if (padding < 0) padding = 0;

            for (int p = 0; p < padding; p++) printf(" ");
            printf("%s\n", header);

            reset_colors();
            current_row++;
        }

        // Display all files from this repository
        for (size_t j = 0; j < repo->file_count && current_row < height - 1; j++) {
            const char* filename = extract_filename(repo->dirty_files[j]);
            char truncated_name[256];
            truncate_filename(filename, truncated_name, sizeof(truncated_name), width - 1);

            printf("%s\n", truncated_name);
            current_row++;
        }

        // Add a blank line between repositories (except for the last one)
        if (i < orch->report.repo_count - 1 && current_row < height - 1) {
            printf("\n");
            current_row++;
        }
    }
}

// Draw tree view with hierarchical file structure
void draw_tree_view(interactive_dirty_files_tui_orchestrator_t* orch, int width, int height) {
    int current_row = 1;

    // Title at top (left-aligned, no padding)
    set_color(36); // Cyan color
    set_bold();
    printf("%s (TREE)\n", orch->config.title);
    current_row++;

    // Summary line (compact)
    if (current_row < height - 1) {
        reset_colors();
        printf("Total: %d dirty repos, %d dirty files\n",
               orch->report.total_dirty_repositories, orch->report.total_dirty_files);
        current_row++;
    }

    reset_colors();

    // Display files as trees grouped by repository
    for (size_t i = 0; i < orch->report.repo_count && current_row < height - 1; i++) {
        dirty_repo_t* repo = &orch->report.repositories[i];

        // Repository header
        if (current_row < height - 1) {
            set_color(36); // Cyan color
            set_bold();

            const char* display_name = get_display_repo_name(repo->name, repo->path);
            char header[256];
            snprintf(header, sizeof(header), "Repository: %s", display_name);

            int header_len = strlen(header);
            int padding = (width - header_len) / 2;
            if (padding < 0) padding = 0;

            for (int p = 0; p < padding; p++) printf(" ");
            printf("%s\n", header);

            reset_colors();
            current_row++;
        }

        // Display file tree for this repository
        if (repo->file_tree && repo->file_tree->child_count > 0 && current_row < height - 1) {
            for (size_t j = 0; j < repo->file_tree->child_count && current_row < height - 1; j++) {
                int is_last = (j == repo->file_tree->child_count - 1);
                print_tree_node(repo->file_tree->children[j], 0, is_last,
                               orch->config.tree_prefix, orch->config.tree_last_prefix,
                               orch->config.tree_indent, width - 4, current_row, height - 1);
            }
        }

        // Add a blank line between repositories (except for the last one)
        if (i < orch->report.repo_count - 1 && current_row < height - 1) {
            printf("\n");
            current_row++;
        }
    }
}

// Draw the TUI overlay with current view mode
void draw_tui_overlay(interactive_dirty_files_tui_orchestrator_t* orch) {
    int width, height;
    get_terminal_size(&width, &height);

    clear_screen();
    move_cursor(1, 1);

    if (orch->current_view == VIEW_FLAT) {
        draw_flat_view(orch, width, height);
    } else {
        draw_tree_view(orch, width, height);
    }

    // Fill remaining space with empty lines until we reach the footer position
    int current_row = 1;
    // Count rows used by content (approximate)
    for (size_t i = 0; i < orch->report.repo_count; i++) {
        current_row += 2; // header + summary
        if (orch->current_view == VIEW_FLAT) {
            current_row += orch->report.repositories[i].file_count;
        } else {
            // Tree view uses more lines due to hierarchy
            current_row += orch->report.repositories[i].file_count * 2;
        }
        if (i < orch->report.repo_count - 1) current_row++; // blank line
    }

    while (current_row < height - 1) {
        printf("\n");
        current_row++;
    }

    // Footer at bottom (left-aligned)
    set_color(36); // Cyan color
    const char* view_name = (orch->current_view == VIEW_FLAT) ? "FLAT" : "TREE";
    printf("Q: exit | R: refresh | SPACE: toggle %s/%s view",
           (orch->current_view == VIEW_FLAT) ? "TREE" : "FLAT",
           (orch->current_view == VIEW_FLAT) ? "FLAT" : "TREE");

    reset_colors();
    fflush(stdout);
}

// Execute the interactive-dirty-files-tui module
int interactive_dirty_files_tui_execute(interactive_dirty_files_tui_orchestrator_t* orch) {
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

    // Load initial report
    load_dirty_files_report(orch);

    // Draw the initial TUI overlay
    draw_tui_overlay(orch);

    // Main input loop
    int running = 1;
    time_t last_refresh = time(NULL);

    while (running) {
        // Check for redraw request from signal handler
        if (redraw_needed) {
            redraw_needed = 0;
            draw_tui_overlay(orch);
        }

        // Check for refresh timer
        time_t now = time(NULL);
        if (now - last_refresh >= orch->config.refresh_interval / 1000) {
            if (load_dirty_files_report(orch) == 0) {
                draw_tui_overlay(orch);
            }
            last_refresh = now;
        }

        // Check for keyboard input (non-blocking)
        int c = getchar();
        if (c != EOF) {
            // Check for exit keys
            if (c == 'q' || c == 'Q' || c == 27) { // 27 is Escape
                running = 0;
            } else if (c == 'r' || c == 'R') {
                // Manual refresh
                if (load_dirty_files_report(orch) == 0) {
                    draw_tui_overlay(orch);
                }
                last_refresh = time(NULL);
            } else if (c == ' ') {
                // Toggle view mode
                orch->current_view = (orch->current_view == VIEW_FLAT) ? VIEW_TREE : VIEW_FLAT;
                draw_tui_overlay(orch);
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
    // Get the module path
    char module_path[1024];
    if (!getcwd(module_path, sizeof(module_path))) {
        fprintf(stderr, "Error: Cannot get current working directory\n");
        return 1;
    }

    // Initialize interactive-dirty-files-tui orchestrator
    interactive_dirty_files_tui_orchestrator_t* orch = interactive_dirty_files_tui_init(module_path);
    if (!orch) {
        fprintf(stderr, "Error: Failed to initialize interactive-dirty-files-tui orchestrator\n");
        return 1;
    }

    // Execute interactive-dirty-files-tui module
    int result = interactive_dirty_files_tui_execute(orch);

    // Cleanup
    interactive_dirty_files_tui_cleanup(orch);

    return result;
}
