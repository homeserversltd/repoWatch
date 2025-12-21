#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <termios.h>
#include <regex.h>

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

// Orchestrator structure
typedef struct {
    char* module_path;
    config_t config;
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

    return orch;
}

// Cleanup orchestrator
void orchestrator_cleanup(orchestrator_t* orch) {
    if (orch) {
        free(orch->config.repo_path);
        free(orch->config.config_dir);
        free(orch->config.cache_dir);
        free(orch->module_path);
        free(orch);
    }
}

// Execute children following infinite index pattern
int execute_children(orchestrator_t* orch) {
    printf("repoWatch orchestrator initialized\n");
    printf("Repository path: %s\n", orch->config.repo_path);
    printf("Config directory: %s\n", orch->config.config_dir);
    printf("Cache directory: %s\n", orch->config.cache_dir);
    printf("Executing children...\n");

    // For demo, we'll manually define the children since we don't have JSON parsing yet
    // In a full implementation, this would be parsed from index.json
    const char* children[] = {"hello", "git", "fs", "tui", "animation"};
    size_t num_children = sizeof(children) / sizeof(children[0]);

    for (size_t i = 0; i < num_children; i++) {
        const char* child_name = children[i];
        char child_cmd[1024];

        // Try different executable naming patterns
        // Pattern 1: {child_name}/{child_name} (e.g., hello/hello)
        snprintf(child_cmd, sizeof(child_cmd), "%s/%s/%s", orch->module_path, child_name, child_name);

        if (access(child_cmd, X_OK) == 0) {
            printf("Executing child: %s\n", child_name);
            int result = system(child_cmd);
            if (result != 0) {
                fprintf(stderr, "Warning: Child '%s' exited with code %d\n", child_name, result);
            }
            continue;
        }

        // Pattern 2: {child_name}/index (for Python-style modules)
        snprintf(child_cmd, sizeof(child_cmd), "%s/%s/index", orch->module_path, child_name);

        if (access(child_cmd, X_OK) == 0) {
            printf("Executing child: %s (index)\n", child_name);
            int result = system(child_cmd);
            if (result != 0) {
                fprintf(stderr, "Warning: Child '%s' exited with code %d\n", child_name, result);
            }
            continue;
        }

        printf("Child '%s' not found or not executable\n", child_name);
    }

    return 0;
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

    // Run main application loop
    if (result == 0) {
        result = run_main_loop();
    }

    // Cleanup
    orchestrator_cleanup(orch);

    return result;
}
