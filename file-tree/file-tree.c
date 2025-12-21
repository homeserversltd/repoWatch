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

// Global flag for redraw requests
volatile sig_atomic_t redraw_needed = 0;

// Signal handler for window resize
void handle_sigwinch(int sig) {
    redraw_needed = 1;
}

// Configuration for file-tree module
typedef struct {
    char* title;
    char* exit_keys;
    int refresh_interval;
    int max_display_files;
    char* report_file;
    struct {
        char* branch;
        char* last_branch;
        char* vertical;
        char* space;
    } tree_symbols;
} file_tree_config_t;

// Orchestrator for file-tree module
typedef struct {
    char* module_path;
    file_tree_config_t config;
} file_tree_orchestrator_t;

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
int load_config(file_tree_orchestrator_t* orch) {
    // Load JSON config from module directory
    char config_path[1024];
    snprintf(config_path, sizeof(config_path), "%s/index.json", orch->module_path);
    json_value_t* config = json_parse_file(config_path);
    if (!config || config->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load config from %s\n", config_path);
        return -1;
    }

    // Extract config values
    orch->config.title = expandvars("File Tree Analysis");
    orch->config.exit_keys = strdup("qQ");
    orch->config.refresh_interval = 5000;
    orch->config.max_display_files = 50;
    orch->config.report_file = expandvars("dirty-files-report.json");

    // Tree symbols
    orch->config.tree_symbols.branch = strdup("├── ");
    orch->config.tree_symbols.last_branch = strdup("└── ");
    orch->config.tree_symbols.vertical = strdup("│   ");
    orch->config.tree_symbols.space = strdup("    ");

    json_free(config);
    return 0;
}

// Print tree node with proper indentation
void print_tree_node(file_tree_node_t* node, int depth, int is_last, const char* prefix,
                     const file_tree_config_t* config, int* current_row, int max_height) {
    if (*current_row >= max_height - 1) return;

    // Print indentation and tree symbols
    printf("%s", prefix);

    if (depth > 0) {
        printf("%s", is_last ? config->tree_symbols.last_branch : config->tree_symbols.branch);
    }

    // Print node name
    if (node->is_file) {
        printf("%s\n", node->name);
    } else {
        printf("%s/\n", node->name);
    }
    (*current_row)++;

    if (*current_row >= max_height - 1) return;

    // Print children
    for (size_t i = 0; i < node->child_count; i++) {
        if (*current_row >= max_height - 1) return;

        // Build prefix for child
        char* child_prefix = malloc(strlen(prefix) + strlen(config->tree_symbols.vertical) + 1);
        if (child_prefix) {
            strcpy(child_prefix, prefix);
            if (depth > 0) {
                strcat(child_prefix, is_last ? config->tree_symbols.space : config->tree_symbols.vertical);
            }
            print_tree_node(node->children[i], depth + 1, i == node->child_count - 1,
                          child_prefix, config, current_row, max_height);
            free(child_prefix);
        }
    }
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

// Draw the TUI overlay with file tree data
void draw_tui_overlay(file_tree_orchestrator_t* orch) {
    int width, height;
    get_terminal_size(&width, &height);

    clear_screen();
    move_cursor(1, 1);

    // Set color for header
    set_color(36); // Cyan color
    set_bold();

    int current_row = 1;

    // Title at top (left-aligned, no padding)
    printf("%s\n", orch->config.title);
    current_row++;

    reset_colors();

    // Load and process dirty files report
    json_value_t* report_json = json_parse_file(orch->config.report_file);
    if (!report_json) {
        printf("Failed to load dirty files report\n");
        current_row++;
        goto footer;
    }

    file_tree_report_t* tree_report = json_process_dirty_files_to_tree(report_json);
    json_free(report_json);

    if (!tree_report) {
        printf("Failed to process file tree\n");
        current_row++;
        goto footer;
    }

    // Display each repository's tree
    for (size_t i = 0; i < tree_report->repo_count && current_row < height - 1; i++) {
        file_tree_repo_t* repo = &tree_report->repos[i];

        // Repository header
        if (current_row < height - 1) {
            set_color(36); // Cyan color
            set_bold();

            const char* display_name = get_display_repo_name(repo->repo_name, repo->repo_path);
            printf("Repository: %s\n", display_name);
            reset_colors();
            current_row++;
        }

        // Print tree starting from root children (skip the empty root node)
        for (size_t j = 0; j < repo->root->child_count && current_row < height - 1; j++) {
            print_tree_node(repo->root->children[j], 0, j == repo->root->child_count - 1,
                          "", &orch->config, &current_row, height - 1);
        }

        // Add spacing between repositories
        if (i < tree_report->repo_count - 1 && current_row < height - 1) {
            printf("\n");
            current_row++;
        }
    }

    file_tree_free(tree_report);

footer:
    // Fill remaining space
    while (current_row < height - 1) {
        printf("\n");
        current_row++;
    }

    // Footer at bottom (left-aligned)
    set_color(36); // Cyan color
    printf("Press Q to exit, press R to refresh");

    reset_colors();
    fflush(stdout);
}

// Initialize orchestrator
file_tree_orchestrator_t* file_tree_init(const char* module_path) {
    file_tree_orchestrator_t* orch = calloc(1, sizeof(file_tree_orchestrator_t));
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

    return orch;
}

// Cleanup orchestrator
void file_tree_cleanup(file_tree_orchestrator_t* orch) {
    if (orch) {
        free(orch->config.title);
        free(orch->config.exit_keys);
        free(orch->config.report_file);
        free(orch->config.tree_symbols.branch);
        free(orch->config.tree_symbols.last_branch);
        free(orch->config.tree_symbols.vertical);
        free(orch->config.tree_symbols.space);
        free(orch->module_path);
        free(orch);
    }
}

// Execute the file-tree module
int file_tree_execute(file_tree_orchestrator_t* orch) {
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
            draw_tui_overlay(orch);
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
                draw_tui_overlay(orch);
                last_refresh = time(NULL);
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

    // Initialize file-tree orchestrator
    file_tree_orchestrator_t* orch = file_tree_init(module_path);
    if (!orch) {
        fprintf(stderr, "Error: Failed to initialize file-tree orchestrator\n");
        return 1;
    }

    // Execute file-tree module
    int result = file_tree_execute(orch);

    // Cleanup
    file_tree_cleanup(orch);

    return result;
}
