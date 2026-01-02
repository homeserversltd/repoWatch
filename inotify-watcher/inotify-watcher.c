#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <regex.h>
#include "../json-utils/json-utils.h"

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
        result = (char*)malloc(strlen(temp) * 2 + 1);
        if (!result) {
            regfree(&regex);
            free(temp);
            return NULL;
        }
        result[0] = '\0';

        const char* p = temp;
        char* out = result;

        while (regexec(&regex, p, 2, matches, 0) == 0) {
            strncat(out, p, matches[0].rm_so);
            out += matches[0].rm_so;

            char var_expr[256];
            size_t len = matches[1].rm_eo - matches[1].rm_so;
            if (len < sizeof(var_expr)) {
                strncpy(var_expr, p + matches[1].rm_so, len);
                var_expr[len] = '\0';

                char* value = NULL;
                if (strstr(var_expr, ":-")) {
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

        strcat(out, p);
        regfree(&regex);
        free(temp);
    }

    return result;
}

// Get daemon PID from PID file
pid_t get_daemon_pid(const char* pid_file_path) {
    if (!pid_file_path) return -1;
    
    FILE* fp = fopen(pid_file_path, "r");
    if (!fp) {
        return -1; // PID file doesn't exist
    }
    
    char pid_str[32];
    if (fgets(pid_str, sizeof(pid_str), fp) == NULL) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    
    pid_t pid = (pid_t)atoi(pid_str);
    
    // Check if process is still running
    if (kill(pid, 0) != 0) {
        // Process doesn't exist, remove stale PID file
        unlink(pid_file_path);
        return -1;
    }
    
    return pid;
}

// Write PID to file
int write_pid_file(const char* pid_file_path, pid_t pid) {
    if (!pid_file_path) return -1;
    
    // Create directory if it doesn't exist
    char* dir_path = strdup(pid_file_path);
    char* last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        // Create parent directories
        char cmd[PATH_MAX];
        snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir_path);
        system(cmd);
    }
    free(dir_path);
    
    FILE* fp = fopen(pid_file_path, "w");
    if (!fp) {
        return -1;
    }
    
    fprintf(fp, "%d\n", (int)pid);
    fclose(fp);
    
    return 0;
}

// Start daemon process
int start_daemon(const char* daemon_path, const char* pid_file_path) {
    if (!daemon_path || !pid_file_path) return -1;
    
    // Check if daemon executable exists
    if (access(daemon_path, X_OK) != 0) {
        fprintf(stderr, "ERROR: Daemon executable not found: %s\n", daemon_path);
        return -1;
    }
    
    // Fork and exec daemon
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    
    if (pid == 0) {
        // Child process - exec daemon
        execl(daemon_path, "inotify-daemon", (char*)NULL);
        perror("execl");
        exit(1);
    }
    
    // Parent process - wait a moment for daemon to start
    usleep(500000); // 500ms
    
    // Check if daemon is still running
    if (kill(pid, 0) != 0) {
        fprintf(stderr, "ERROR: Daemon failed to start\n");
        return -1;
    }
    
    // Write PID file
    if (write_pid_file(pid_file_path, pid) != 0) {
        fprintf(stderr, "WARNING: Failed to write PID file\n");
    }
    
    return 0;
}

// Send signal to daemon
int ping_daemon(pid_t pid) {
    if (pid <= 0) return -1;
    
    if (kill(pid, SIGUSR1) != 0) {
        perror("kill");
        return -1;
    }
    
    return 0;
}

// Load configuration from index.json
int load_config(char** pid_file_path, char** report_file_path, char** git_submodules_report) {
    json_value_t* config = json_parse_file("index.json");
    if (!config || config->type != JSON_OBJECT) {
        return -1;
    }
    
    json_value_t* paths = get_nested_value(config, "paths");
    if (paths && paths->type == JSON_OBJECT) {
        json_value_t* pid_file = get_nested_value(paths, "pid_file");
        json_value_t* report_file = get_nested_value(paths, "report_file");
        json_value_t* git_report = get_nested_value(paths, "git_submodules_report");
        
        if (pid_file && pid_file->type == JSON_STRING) {
            *pid_file_path = expandvars(pid_file->value.str_val);
        }
        if (report_file && report_file->type == JSON_STRING) {
            *report_file_path = strdup(report_file->value.str_val);
        }
        if (git_report && git_report->type == JSON_STRING) {
            *git_submodules_report = strdup(git_report->value.str_val);
        }
    }
    
    json_free(config);
    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    // Get current directory (should be inotify-watcher directory)
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "ERROR: Cannot get current working directory\n");
        return 1;
    }
    
    // Change to component directory if we're in repoWatch root
    if (strstr(cwd, "inotify-watcher") == NULL) {
        char component_path[PATH_MAX];
        snprintf(component_path, sizeof(component_path), "%s/inotify-watcher", cwd);
        if (chdir(component_path) != 0) {
            fprintf(stderr, "ERROR: Cannot change to inotify-watcher directory\n");
            return 1;
        }
    }
    
    // Load configuration
    char* pid_file_path = NULL;
    char* report_file_path = NULL;
    char* git_submodules_report = NULL;
    
    if (load_config(&pid_file_path, &report_file_path, &git_submodules_report) != 0) {
        fprintf(stderr, "ERROR: Failed to load configuration from index.json\n");
        return 1;
    }
    
    // Default values if not in config
    if (!pid_file_path) {
        const char* cache_home = getenv("XDG_CACHE_HOME");
        if (!cache_home) {
            cache_home = "~/.cache";
        }
        char default_pid[PATH_MAX];
        snprintf(default_pid, sizeof(default_pid), "%s/repowatch/inotify-daemon.pid", cache_home);
        pid_file_path = expandvars(default_pid);
    }
    if (!report_file_path) {
        report_file_path = strdup("inotify-changes-report.json");
    }
    if (!git_submodules_report) {
        git_submodules_report = strdup("../git-submodules.report");
    }
    
    // Expand paths
    if (pid_file_path) {
        char* expanded = expandvars(pid_file_path);
        if (expanded) {
            free(pid_file_path);
            pid_file_path = expanded;
        }
    }
    
    // Check if daemon is running
    pid_t daemon_pid = get_daemon_pid(pid_file_path);
    
    if (daemon_pid < 0) {
        // Daemon not running - start it
        // Get absolute path to daemon executable
        char daemon_path[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            snprintf(daemon_path, sizeof(daemon_path), "%s/inotify-daemon", cwd);
        } else {
            // Fallback to relative path
            strncpy(daemon_path, "inotify-daemon", sizeof(daemon_path) - 1);
            daemon_path[sizeof(daemon_path) - 1] = '\0';
        }
        
        if (start_daemon(daemon_path, pid_file_path) != 0) {
            fprintf(stderr, "ERROR: Failed to start daemon\n");
            free(pid_file_path);
            free(report_file_path);
            free(git_submodules_report);
            return 1;
        }
        
        // Get PID after starting
        daemon_pid = get_daemon_pid(pid_file_path);
        if (daemon_pid < 0) {
            fprintf(stderr, "ERROR: Daemon started but PID file not found\n");
            free(pid_file_path);
            free(report_file_path);
            free(git_submodules_report);
            return 1;
        }
        
        // Wait a bit longer for daemon to initialize watches
        usleep(1000000); // 1 second
    }
    
    // Send signal to daemon to trigger report update
    if (ping_daemon(daemon_pid) != 0) {
        fprintf(stderr, "ERROR: Failed to send signal to daemon\n");
        free(pid_file_path);
        free(report_file_path);
        free(git_submodules_report);
        return 1;
    }
    
    // Wait briefly for report to be written
    usleep(200000); // 200ms
    
    // Cleanup
    free(pid_file_path);
    free(report_file_path);
    free(git_submodules_report);
    
    return 0;
}
