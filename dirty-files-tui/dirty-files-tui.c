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

// Structure for dirty repository data
typedef struct {
    char* name;
    char* path;
    int dirty_file_count;
    char** dirty_files;
    size_t file_count;
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

// Configuration for dirty-files-tui module
typedef struct {
    char* title;
    char* exit_keys;
    int refresh_interval;
    int max_display_files;
    char* report_file;
} dirty_files_tui_config_t;

// Global flag for redraw requests
volatile sig_atomic_t redraw_needed = 0;

// Signal handler for window resize
void handle_sigwinch(int sig) {
    redraw_needed = 1;
}

// Orchestrator for dirty-files-tui module
typedef struct {
    char* module_path;
    dirty_files_tui_config_t config;
    dirty_files_report_t report;
} dirty_files_tui_orchestrator_t;

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
int load_config(dirty_files_tui_orchestrator_t* orch) {
    // Load JSON config
    json_value_t* config = json_parse_file("dirty-files-tui/index.json");
    if (!config || config->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load config\n");
        return -1;
    }

    // Extract config values
    orch->config.title = expandvars("Dirty Files Analysis");
    orch->config.exit_keys = strdup("qQ");
    orch->config.refresh_interval = 5000;
    orch->config.max_display_files = 50;
    orch->config.report_file = expandvars("dirty-files-report.json");

    json_free(config);
    return 0;
}

// Load dirty files report from JSON
int load_dirty_files_report(dirty_files_tui_orchestrator_t* orch) {
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
dirty_files_tui_orchestrator_t* dirty_files_tui_init(const char* module_path) {
    dirty_files_tui_orchestrator_t* orch = calloc(1, sizeof(dirty_files_tui_orchestrator_t));
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
void dirty_files_tui_cleanup(dirty_files_tui_orchestrator_t* orch) {
    if (orch) {
        cleanup_report(&orch->report);
        free(orch->config.title);
        free(orch->config.exit_keys);
        free(orch->config.report_file);
        free(orch->module_path);
        free(orch);
    }
}

// Draw the TUI overlay with dirty files data
void draw_tui_overlay(dirty_files_tui_orchestrator_t* orch) {
    int width, height;
    get_terminal_size(&width, &height);

    clear_screen();
    move_cursor(1, 1);

    // Draw border
    set_color(36); // Cyan color
    set_bold();

    // Top border
    printf("┌");
    for (int i = 0; i < width - 2; i++) printf("─");
    printf("┐\n");

    // Side borders with content
    int current_row = 2;

    // Title
    printf("│");
    int title_len = strlen(orch->config.title);
    int padding = (width - 2 - title_len) / 2;
    for (int i = 0; i < padding; i++) printf(" ");
    printf("%s", orch->config.title);
    for (int i = 0; i < width - 2 - padding - title_len; i++) printf(" ");
    printf("│\n");
    current_row++;

    // Summary
    if (current_row < height - 1) {
        printf("│");
        char summary[256];
        snprintf(summary, sizeof(summary), "Total: %d dirty repos, %d dirty files",
                 orch->report.total_dirty_repositories, orch->report.total_dirty_files);
        int summary_len = strlen(summary);
        padding = (width - 2 - summary_len) / 2;
        for (int i = 0; i < padding; i++) printf(" ");
        printf("%s", summary);
        for (int i = 0; i < width - 2 - padding - summary_len; i++) printf(" ");
        printf("│\n");
        current_row++;
    }

    // Repository list
    for (size_t i = 0; i < orch->report.repo_count && current_row < height - 3; i++) {
        dirty_repo_t* repo = &orch->report.repositories[i];

        if (current_row < height - 1) {
            printf("│");
            char repo_info[256];
            snprintf(repo_info, sizeof(repo_info), "%s: %d files", repo->name, repo->dirty_file_count);
            int repo_len = strlen(repo_info);
            padding = 2;
            for (int i = 0; i < padding; i++) printf(" ");
            printf("%s", repo_info);
            for (int i = 0; i < width - 2 - padding - repo_len; i++) printf(" ");
            printf("│\n");
            current_row++;
        }

        // Show some files
        int files_to_show = repo->file_count > 3 ? 3 : repo->file_count;
        for (int j = 0; j < files_to_show && current_row < height - 3; j++) {
            printf("│");
            padding = 4;
            for (int i = 0; i < padding; i++) printf(" ");
            printf("• %s", repo->dirty_files[j]);
            for (int i = 0; i < width - 2 - padding - 2 - (int)strlen(repo->dirty_files[j]); i++) printf(" ");
            printf("│\n");
            current_row++;
        }

        if (repo->file_count > 3 && current_row < height - 3) {
            printf("│");
            padding = 4;
            for (int i = 0; i < padding; i++) printf(" ");
            char more_msg[64];
            snprintf(more_msg, sizeof(more_msg), "... and %zu more", repo->file_count - 3);
            printf("%s", more_msg);
            for (int i = 0; i < width - 2 - padding - (int)strlen(more_msg); i++) printf(" ");
            printf("│\n");
            current_row++;
        }
    }

    // Fill remaining space
    while (current_row < height - 2) {
        printf("│");
        for (int i = 0; i < width - 2; i++) printf(" ");
        printf("│\n");
        current_row++;
    }

    // Instructions
    if (current_row < height - 1) {
        printf("│");
        const char* instr = "Press Q to exit, R to refresh";
        int instr_len = strlen(instr);
        padding = (width - 2 - instr_len) / 2;
        for (int i = 0; i < padding; i++) printf(" ");
        printf("%s", instr);
        for (int i = 0; i < width - 2 - padding - instr_len; i++) printf(" ");
        printf("│\n");
        current_row++;
    }

    // Bottom border
    printf("└");
    for (int i = 0; i < width - 2; i++) printf("─");
    printf("┘\n");

    reset_colors();
    fflush(stdout);
}

// Execute the dirty-files-tui module
int dirty_files_tui_execute(dirty_files_tui_orchestrator_t* orch) {
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

    // Initialize dirty-files-tui orchestrator
    dirty_files_tui_orchestrator_t* orch = dirty_files_tui_init(module_path);
    if (!orch) {
        fprintf(stderr, "Error: Failed to initialize dirty-files-tui orchestrator\n");
        return 1;
    }

    // Execute dirty-files-tui module
    int result = dirty_files_tui_execute(orch);

    // Cleanup
    dirty_files_tui_cleanup(orch);

    return result;
}
