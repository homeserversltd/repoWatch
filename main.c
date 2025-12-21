#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <termios.h>
#include <regex.h>
#include <time.h>
#include <stdarg.h>
#include "json-utils/json-utils.h"

// Configuration structure following infinite index pattern
typedef struct {
    char* repo_path;
    char* config_dir;
    char* cache_dir;
    int session_timeout;
    int max_commits;
    int animation_fps;
    int ui_refresh_rate;
    int terminal_width;
    int terminal_height;
} config_t;

// Child state structure for state aggregation
typedef struct {
    char* name;
    int exit_code;
    time_t start_time;
    time_t end_time;
    char* report;
} child_state_t;

// Root state structure
typedef struct {
    child_state_t* children;
    size_t num_children;
    time_t session_start;
    time_t session_end;
    FILE* log_file;
} root_state_t;

// Orchestrator structure
typedef struct {
    char* module_path;
    config_t config;
    root_state_t state;
} orchestrator_t;

// Custom environment variable expansion (handles ${VAR:-default} syntax)
char* expandvars(const char* input) {
    if (!input) return NULL;

    char* result = strdup(input);
    if (!result) return NULL;

    // Handle ${VAR:-default} pattern
    regex_t regex;
    regmatch_t matches[2];

    if (regcomp(&regex, "\\$\\{([^}]+)\\}", REG_EXTENDED) == 0) {
        char* temp = result;
        result = (char*)malloc(strlen(temp) * 2 + 1); // Allocate extra space
        if (!result) {
            regfree(&regex);
            free(temp);
            return NULL;
        }
        result[0] = '\0';

        const char* p = temp;
        char* out = result;

        while (regexec(&regex, p, 2, matches, 0) == 0) {
            // Copy text before match
            strncat(out, p, matches[0].rm_so);
            out += matches[0].rm_so;

            // Extract variable expression
            char var_expr[256];
            size_t len = matches[1].rm_eo - matches[1].rm_so;
            if (len < sizeof(var_expr)) {
                strncpy(var_expr, p + matches[1].rm_so, len);
                var_expr[len] = '\0';

                char* value = NULL;
                if (strstr(var_expr, ":-")) {
                    // Handle ${VAR:-default} syntax
                    char* colon = strstr(var_expr, ":-");
                    *colon = '\0';
                    char* var_name = var_expr;
                    char* default_val = colon + 2;
                    value = getenv(var_name);
                    if (!value) value = default_val;
                } else {
                    value = getenv(var_expr);
                }

                if (value) {
                    strcat(out, value);
                    out += strlen(value);
                }
            }

            p += matches[0].rm_eo;
        }

        // Copy remaining text
        strcat(out, p);

        regfree(&regex);
        free(temp);
    }

    return result;
}

// Load configuration from index.json
int load_config(orchestrator_t* orch) {
    char config_path[1024];
    snprintf(config_path, sizeof(config_path), "%s/index.json", orch->module_path);

    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open config file %s\n", config_path);
        return -1;
    }

    // For now, use hardcoded defaults since we don't have JSON parsing
    // In a full implementation, you'd parse the JSON here
    orch->config.repo_path = expandvars("${REPO_WATCH_REPO_PATH:-.}");
    orch->config.config_dir = expandvars("${XDG_CONFIG_HOME:-~/.config}/repowatch");
    orch->config.cache_dir = expandvars("${XDG_CACHE_HOME:-~/.cache}/repowatch");
    orch->config.session_timeout = 3600;
    orch->config.max_commits = 20;
    orch->config.animation_fps = 10;
    orch->config.ui_refresh_rate = 2;
    orch->config.terminal_width = 120;
    orch->config.terminal_height = 30;

    fclose(fp);
    return 0;
}

// Logging function for state tracking
void log_state(orchestrator_t* orch, const char* message, ...) {
    if (!orch->state.log_file) return;

    va_list args;
    va_start(args, message);

    time_t now = time(NULL);
    char timestamp[26];
    ctime_r(&now, timestamp);
    timestamp[strlen(timestamp) - 1] = '\0'; // Remove newline

    fprintf(orch->state.log_file, "[%s] ", timestamp);
    vfprintf(orch->state.log_file, message, args);
    fprintf(orch->state.log_file, "\n");
    fflush(orch->state.log_file);

    va_end(args);
}

// Initialize orchestrator
orchestrator_t* orchestrator_init(const char* module_path) {
    orchestrator_t* orch = calloc(1, sizeof(orchestrator_t));
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

    // Initialize state
    orch->state.children = NULL;
    orch->state.num_children = 0;
    orch->state.session_start = time(NULL);
    orch->state.session_end = 0;

    // Open log file
    char log_path[1024];
    snprintf(log_path, sizeof(log_path), "%s/session.log", orch->module_path);
    orch->state.log_file = fopen(log_path, "w");
    if (orch->state.log_file) {
        log_state(orch, "Session started - Module path: %s", orch->module_path);
        log_state(orch, "Config loaded - Repo: %s, Cache: %s", orch->config.repo_path, orch->config.cache_dir);
    }

    return orch;
}

// Add child state to orchestrator
void add_child_state(orchestrator_t* orch, const char* name, int exit_code, time_t start_time, time_t end_time, const char* report) {
    orch->state.children = realloc(orch->state.children, sizeof(child_state_t) * (orch->state.num_children + 1));
    if (!orch->state.children) return;

    child_state_t* child = &orch->state.children[orch->state.num_children];
    child->name = strdup(name);
    child->exit_code = exit_code;
    child->start_time = start_time;
    child->end_time = end_time;
    child->report = report ? strdup(report) : NULL;

    orch->state.num_children++;
}

// Cleanup orchestrator
void orchestrator_cleanup(orchestrator_t* orch) {
    if (orch) {
        // Log session end
        if (orch->state.log_file) {
            log_state(orch, "Session ended - Total children executed: %zu", orch->state.num_children);
            fclose(orch->state.log_file);
        }

        // Cleanup config
        free(orch->config.repo_path);
        free(orch->config.config_dir);
        free(orch->config.cache_dir);

        // Cleanup state
        if (orch->state.children) {
            for (size_t i = 0; i < orch->state.num_children; i++) {
                free(orch->state.children[i].name);
                free(orch->state.children[i].report);
            }
            free(orch->state.children);
        }

        free(orch->module_path);
        free(orch);
    }
}

// Execute children following infinite index pattern
int execute_children(orchestrator_t* orch) {
    log_state(orch, "Beginning child execution phase");

    printf("repoWatch orchestrator initialized\n");
    printf("Repository path: %s\n", orch->config.repo_path);
    printf("Config directory: %s\n", orch->config.config_dir);
    printf("Cache directory: %s\n", orch->config.cache_dir);
    printf("Executing children...\n");

    // Read children from index.json using external utility
    FILE* child_fp = popen("./json-utils/get-children . 2>/dev/null", "r");
    if (!child_fp) {
        fprintf(stderr, "Error: Could not execute get-children utility\n");
        return 1;
    }

    // Read the output line
    char line[1024];
    size_t num_children = 0;
    char** children = NULL;

    if (fgets(line, sizeof(line), child_fp)) {
        // Remove trailing newline
        char* newline = strchr(line, '\n');
        if (newline) *newline = '\0';

        // Count children (space-separated)
        char temp_line[1024];
        strcpy(temp_line, line);
        char* token = strtok(temp_line, " ");
        while (token) {
            num_children++;
            token = strtok(NULL, " ");
        }

        if (num_children > 0) {
            children = malloc(sizeof(char*) * num_children);
            if (children) {
                token = strtok(line, " ");
                size_t i = 0;
                while (token && i < num_children) {
                    children[i] = strdup(token);
                    token = strtok(NULL, " ");
                    i++;
                }
            }
        }
    }

    pclose(child_fp);

    if (!children || num_children == 0) {
        log_state(orch, "ERROR: No children found in index.json");
        fprintf(stderr, "Error: Could not read children from index.json\n");
        if (children) free(children);
        return 1;
    }

    log_state(orch, "Found %zu children to execute: %s", num_children, line);

    for (size_t i = 0; i < num_children; i++) {
        const char* child_name = children[i];
        char child_cmd[1024];
        char report_file[1024];

        // Create path for child's report file
        snprintf(report_file, sizeof(report_file), "%s/%s/.report", orch->module_path, child_name);

        // Try different executable naming patterns
        // Pattern 1: {child_name}/{child_name} (e.g., hello/hello)
        snprintf(child_cmd, sizeof(child_cmd), "%s/%s/%s", orch->module_path, child_name, child_name);

        if (access(child_cmd, X_OK) == 0) {
            log_state(orch, "Executing child: %s (pattern 1: %s)", child_name, child_cmd);
            printf("Executing child: %s\n", child_name);

            time_t start_time = time(NULL);
            int result = system(child_cmd);
            time_t end_time = time(NULL);

            if (result != 0) {
                log_state(orch, "WARNING: Child '%s' exited with code %d (took %ld seconds)", child_name, result, end_time - start_time);
                fprintf(stderr, "Warning: Child '%s' exited with code %d\n", child_name, result);
            } else {
                log_state(orch, "SUCCESS: Child '%s' completed successfully (took %ld seconds)", child_name, end_time - start_time);
            }

            // Read child's report if it exists
            char* report = NULL;
            FILE* rf = fopen(report_file, "r");
            if (rf) {
                fseek(rf, 0, SEEK_END);
                long size = ftell(rf);
                fseek(rf, 0, SEEK_SET);
                report = malloc(size + 1);
                if (report) {
                    fread(report, 1, size, rf);
                    report[size] = '\0';
                }
                fclose(rf);
                // Clean up report file
                unlink(report_file);
            }

            // Add to state
            add_child_state(orch, child_name, result, start_time, end_time, report);
            free(report);
            continue;
        }

        // Pattern 2: {child_name}/index (for Python-style modules)
        snprintf(child_cmd, sizeof(child_cmd), "%s/%s/index", orch->module_path, child_name);

        if (access(child_cmd, X_OK) == 0) {
            log_state(orch, "Executing child: %s (pattern 2: %s)", child_name, child_cmd);
            printf("Executing child: %s (index)\n", child_name);

            time_t start_time = time(NULL);
            int result = system(child_cmd);
            time_t end_time = time(NULL);

            if (result != 0) {
                log_state(orch, "WARNING: Child '%s' exited with code %d (took %ld seconds)", child_name, result, end_time - start_time);
                fprintf(stderr, "Warning: Child '%s' exited with code %d\n", child_name, result);
            } else {
                log_state(orch, "SUCCESS: Child '%s' completed successfully (took %ld seconds)", child_name, end_time - start_time);
            }

            // Read child's report if it exists
            char* report = NULL;
            FILE* rf = fopen(report_file, "r");
            if (rf) {
                fseek(rf, 0, SEEK_END);
                long size = ftell(rf);
                fseek(rf, 0, SEEK_SET);
                report = malloc(size + 1);
                if (report) {
                    fread(report, 1, size, rf);
                    report[size] = '\0';
                }
                fclose(rf);
                // Clean up report file
                unlink(report_file);
            }

            // Add to state
            add_child_state(orch, child_name, result, start_time, end_time, report);
            free(report);
            continue;
        }

        log_state(orch, "Child '%s' not found or not executable (tried patterns: %s/%s and %s/%s/index)",
                 child_name, orch->module_path, child_name, orch->module_path, child_name);
        printf("Child '%s' not found or not executable\n", child_name);
    }

    // Cleanup children array
    for (size_t i = 0; i < num_children; i++) {
        free(children[i]);
    }
    free(children);

    return 0;
}

// Display aggregated child reports
void display_child_reports(orchestrator_t* orch) {
    log_state(orch, "Displaying child execution reports for %zu children", orch->state.num_children);

    printf("\n=== CHILD EXECUTION REPORTS ===\n");

    if (orch->state.num_children == 0) {
        log_state(orch, "No children were executed");
        printf("No children executed.\n");
        return;
    }

    for (size_t i = 0; i < orch->state.num_children; i++) {
        child_state_t* child = &orch->state.children[i];

        printf("\nChild: %s\n", child->name);
        printf("Exit Code: %d\n", child->exit_code);
        printf("Execution Time: %ld seconds\n", child->end_time - child->start_time);

        if (child->report && strlen(child->report) > 0) {
            printf("Report: %s", child->report);
        } else {
            printf("Report: (no report provided)\n");
        }
    }

    printf("\n=== END REPORTS ===\n");
    sleep(2); // Display for 2 seconds as requested
}

// Main application loop with terminal handling
int run_main_loop() {
    printf("Hey, Zig. Fire.\n");
    printf("Press Q, Escape, or Ctrl+C to exit...\n");

    // Get current terminal settings
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;

    // Disable canonical mode and echo
    new_tio.c_lflag &= (~ICANON & ~ECHO);

    // Set the new settings
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    int result = 0;
    while (1) {
        int c = getchar();

        // Check for Q, q, Escape (27), or Ctrl+C (3)
        if (c == 'q' || c == 'Q' || c == 27 || c == 3) {
            break;
        }
    }

    // Restore old terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

    printf("Goodbye!\n");
    return result;
}

int main(int argc, char* argv[]) {
    // Get the module path (directory containing this executable)
    char module_path[1024];
    if (!getcwd(module_path, sizeof(module_path))) {
        fprintf(stderr, "Error: Cannot get current working directory\n");
        return 1;
    }

    // Initialize orchestrator (infinite index pattern)
    orchestrator_t* orch = orchestrator_init(module_path);
    if (!orch) {
        fprintf(stderr, "Error: Failed to initialize orchestrator\n");
        return 1;
    }

    // Execute children (placeholder for now)
    int result = execute_children(orch);

    // Set session end time
    orch->state.session_end = time(NULL);

    log_state(orch, "Child execution phase completed with result: %d", result);

    // Display child reports before main loop
    display_child_reports(orch);

    log_state(orch, "Starting main application loop");

    // Run main application loop
    if (result == 0) {
        result = run_main_loop();
        log_state(orch, "Main application loop exited with result: %d", result);
    } else {
        log_state(orch, "Skipping main loop due to child execution failure");
    }

    // Cleanup
    orchestrator_cleanup(orch);

    return result;
}