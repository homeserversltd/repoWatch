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
#include <sys/resource.h>
#include <sys/time.h>
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

// Benchmarking structures for performance tracking
typedef struct {
    double wall_time_sec;      // Wall clock time (high precision)
    double cpu_time_user_sec;  // User CPU time
    double cpu_time_sys_sec;   // System CPU time
    long memory_rss_kb;        // Resident set size (RSS) in KB
    long memory_vms_kb;        // Virtual memory size in KB
    long io_read_bytes;       // Bytes read from disk
    long io_write_bytes;       // Bytes written to disk
    long io_read_ops;          // Read operations
    long io_write_ops;         // Write operations
} benchmark_metrics_t;

typedef struct {
    char* component_name;
    benchmark_metrics_t metrics;
    int exit_code;
} component_benchmark_t;

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

// Get current process resource usage metrics
benchmark_metrics_t get_current_metrics(void) {
    benchmark_metrics_t metrics = {0};

    // Get CPU time and I/O stats from getrusage
    struct rusage ru;
    if (getrusage(RUSAGE_CHILDREN, &ru) == 0) {
        // CPU time (convert from microseconds to seconds)
        metrics.cpu_time_user_sec = (double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec / 1000000.0;
        metrics.cpu_time_sys_sec = (double)ru.ru_stime.tv_sec + (double)ru.ru_stime.tv_usec / 1000000.0;

        // I/O operations (512-byte blocks, convert to bytes)
        metrics.io_read_bytes = ru.ru_inblock * 512;
        metrics.io_write_bytes = ru.ru_oublock * 512;
        metrics.io_read_ops = ru.ru_inblock;
        metrics.io_write_ops = ru.ru_oublock;
    }

    // Get memory usage from /proc/self/status
    FILE* proc_file = fopen("/proc/self/status", "r");
    if (proc_file) {
        char line[256];
        while (fgets(line, sizeof(line), proc_file)) {
            if (strncmp(line, "VmRSS:", 6) == 0) {
                sscanf(line, "VmRSS: %ld kB", &metrics.memory_rss_kb);
            } else if (strncmp(line, "VmSize:", 7) == 0) {
                sscanf(line, "VmSize: %ld kB", &metrics.memory_vms_kb);
            }
        }
        fclose(proc_file);
    }

    // Wall time will be set by caller using clock_gettime
    metrics.wall_time_sec = 0.0;

    return metrics;
}

// Calculate delta between two metric snapshots (after - before)
benchmark_metrics_t calculate_delta(benchmark_metrics_t before, benchmark_metrics_t after) {
    benchmark_metrics_t delta = {
        .wall_time_sec = after.wall_time_sec - before.wall_time_sec,
        .cpu_time_user_sec = after.cpu_time_user_sec - before.cpu_time_user_sec,
        .cpu_time_sys_sec = after.cpu_time_sys_sec - before.cpu_time_sys_sec,
        .memory_rss_kb = after.memory_rss_kb - before.memory_rss_kb,
        .memory_vms_kb = after.memory_vms_kb - before.memory_vms_kb,
        .io_read_bytes = after.io_read_bytes - before.io_read_bytes,
        .io_write_bytes = after.io_write_bytes - before.io_write_bytes,
        .io_read_ops = after.io_read_ops - before.io_read_ops,
        .io_write_ops = after.io_write_ops - before.io_write_ops
    };

    // Ensure non-negative values (in case of counter resets)
    if (delta.wall_time_sec < 0) delta.wall_time_sec = 0;
    if (delta.cpu_time_user_sec < 0) delta.cpu_time_user_sec = 0;
    if (delta.cpu_time_sys_sec < 0) delta.cpu_time_sys_sec = 0;
    if (delta.memory_rss_kb < 0) delta.memory_rss_kb = 0;
    if (delta.memory_vms_kb < 0) delta.memory_vms_kb = 0;
    if (delta.io_read_bytes < 0) delta.io_read_bytes = 0;
    if (delta.io_write_bytes < 0) delta.io_write_bytes = 0;
    if (delta.io_read_ops < 0) delta.io_read_ops = 0;
    if (delta.io_write_ops < 0) delta.io_write_ops = 0;

    return delta;
}

// Helper function to compare components by wall time (for qsort)
int compare_by_wall_time(const void* a, const void* b) {
    const component_benchmark_t* ca = (const component_benchmark_t*)a;
    const component_benchmark_t* cb = (const component_benchmark_t*)b;
    if (ca->metrics.wall_time_sec > cb->metrics.wall_time_sec) return -1;
    if (ca->metrics.wall_time_sec < cb->metrics.wall_time_sec) return 1;
    return 0;
}

// Helper function to compare components by CPU time (for qsort)
int compare_by_cpu_time(const void* a, const void* b) {
    const component_benchmark_t* ca = (const component_benchmark_t*)a;
    const component_benchmark_t* cb = (const component_benchmark_t*)b;
    double cpu_a = ca->metrics.cpu_time_user_sec + ca->metrics.cpu_time_sys_sec;
    double cpu_b = cb->metrics.cpu_time_user_sec + cb->metrics.cpu_time_sys_sec;
    if (cpu_a > cpu_b) return -1;
    if (cpu_a < cpu_b) return 1;
    return 0;
}

// Helper function to compare components by memory usage (for qsort)
int compare_by_memory(const void* a, const void* b) {
    const component_benchmark_t* ca = (const component_benchmark_t*)a;
    const component_benchmark_t* cb = (const component_benchmark_t*)b;
    if (ca->metrics.memory_rss_kb > cb->metrics.memory_rss_kb) return -1;
    if (ca->metrics.memory_rss_kb < cb->metrics.memory_rss_kb) return 1;
    return 0;
}

// Helper function to compare components by I/O operations (for qsort)
int compare_by_io(const void* a, const void* b) {
    const component_benchmark_t* ca = (const component_benchmark_t*)a;
    const component_benchmark_t* cb = (const component_benchmark_t*)b;
    long io_a = ca->metrics.io_read_ops + ca->metrics.io_write_ops;
    long io_b = cb->metrics.io_read_ops + cb->metrics.io_write_ops;
    if (io_a > io_b) return -1;
    if (io_a < io_b) return 1;
    return 0;
}

// Create a sorted copy of benchmarks for a specific metric
component_benchmark_t* get_top_components(component_benchmark_t* benchmarks, size_t count, size_t* top_count,
                                        int (*compare_func)(const void*, const void*)) {
    if (count == 0) {
        *top_count = 0;
        return NULL;
    }

    // Create a copy of the benchmarks array
    component_benchmark_t* sorted = malloc(count * sizeof(component_benchmark_t));
    if (!sorted) return NULL;

    for (size_t i = 0; i < count; i++) {
        sorted[i] = benchmarks[i];
        // Deep copy the component name
        sorted[i].component_name = strdup(benchmarks[i].component_name);
    }

    // Sort by the specified metric
    qsort(sorted, count, sizeof(component_benchmark_t), compare_func);

    // Return top 3 (or all if fewer than 3)
    *top_count = (count < 3) ? count : 3;
    return sorted;
}

// Helper function to write component array to JSON file
void write_component_array_json(FILE* fp, const char* section_name, component_benchmark_t* components, size_t comp_count, int add_comma) {
    fprintf(fp, "  \"%s\": [\n", section_name);
    for (size_t i = 0; i < comp_count; i++) {
        const component_benchmark_t* comp = &components[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"name\": \"%s\",\n", comp->component_name);
        fprintf(fp, "      \"wall_time_sec\": %.6f,\n", comp->metrics.wall_time_sec);
        fprintf(fp, "      \"cpu_time_user_sec\": %.6f,\n", comp->metrics.cpu_time_user_sec);
        fprintf(fp, "      \"cpu_time_sys_sec\": %.6f,\n", comp->metrics.cpu_time_sys_sec);
        fprintf(fp, "      \"memory_rss_kb\": %ld,\n", comp->metrics.memory_rss_kb);
        fprintf(fp, "      \"memory_vms_kb\": %ld,\n", comp->metrics.memory_vms_kb);
        fprintf(fp, "      \"io_read_bytes\": %ld,\n", comp->metrics.io_read_bytes);
        fprintf(fp, "      \"io_write_bytes\": %ld,\n", comp->metrics.io_write_bytes);
        fprintf(fp, "      \"io_read_ops\": %ld,\n", comp->metrics.io_read_ops);
        fprintf(fp, "      \"io_write_ops\": %ld,\n", comp->metrics.io_write_ops);
        fprintf(fp, "      \"exit_code\": %d\n", comp->exit_code);
        fprintf(fp, "    }%s\n", (i < comp_count - 1) ? "," : "");
    }
    fprintf(fp, "  ]%s\n", add_comma ? "," : "");
}

// Write benchmark report to JSON file
void write_benchmark_report(orchestrator_t* orch, component_benchmark_t* benchmarks, size_t count) {
    if (!benchmarks || count == 0) return;

    FILE* fp = fopen("benchmark-report.json", "w");
    if (!fp) {
        fprintf(stderr, "Warning: Could not create benchmark report file\n");
        return;
    }

    // Get top components for each category
    size_t slowest_count, cpu_count, memory_count, io_count;
    component_benchmark_t* slowest = get_top_components(benchmarks, count, &slowest_count, compare_by_wall_time);
    component_benchmark_t* most_cpu = get_top_components(benchmarks, count, &cpu_count, compare_by_cpu_time);
    component_benchmark_t* most_memory = get_top_components(benchmarks, count, &memory_count, compare_by_memory);
    component_benchmark_t* most_io = get_top_components(benchmarks, count, &io_count, compare_by_io);

    // Calculate session duration
    double session_duration = difftime(orch->state.session_end, orch->state.session_start);

    // Write JSON header
    fprintf(fp, "{\n");
    fprintf(fp, "  \"session_info\": {\n");
    fprintf(fp, "    \"start_time\": \"%ld\",\n", (long)orch->state.session_start);
    fprintf(fp, "    \"end_time\": \"%ld\",\n", (long)orch->state.session_end);
    fprintf(fp, "    \"total_duration_sec\": %.3f,\n", session_duration);
    fprintf(fp, "    \"components_executed\": %zu\n", count);
    fprintf(fp, "  },\n");

    // Write slowest components
    write_component_array_json(fp, "slowest_components", slowest, slowest_count, 1);

    // Write most CPU intensive
    write_component_array_json(fp, "most_cpu_intensive", most_cpu, cpu_count, 1);

    // Write most memory intensive
    write_component_array_json(fp, "most_memory_intensive", most_memory, memory_count, 1);

    // Write most I/O intensive
    write_component_array_json(fp, "most_io_intensive", most_io, io_count, 1);

    // Write all components
    fprintf(fp, "  \"all_components\": [\n");
    for (size_t i = 0; i < count; i++) {
        const component_benchmark_t* comp = &benchmarks[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"name\": \"%s\",\n", comp->component_name);
        fprintf(fp, "      \"wall_time_sec\": %.6f,\n", comp->metrics.wall_time_sec);
        fprintf(fp, "      \"cpu_time_user_sec\": %.6f,\n", comp->metrics.cpu_time_user_sec);
        fprintf(fp, "      \"cpu_time_sys_sec\": %.6f,\n", comp->metrics.cpu_time_sys_sec);
        fprintf(fp, "      \"memory_rss_kb\": %ld,\n", comp->metrics.memory_rss_kb);
        fprintf(fp, "      \"memory_vms_kb\": %ld,\n", comp->metrics.memory_vms_kb);
        fprintf(fp, "      \"io_read_bytes\": %ld,\n", comp->metrics.io_read_bytes);
        fprintf(fp, "      \"io_write_bytes\": %ld,\n", comp->metrics.io_write_bytes);
        fprintf(fp, "      \"io_read_ops\": %ld,\n", comp->metrics.io_read_ops);
        fprintf(fp, "      \"io_write_ops\": %ld,\n", comp->metrics.io_write_ops);
        fprintf(fp, "      \"exit_code\": %d\n", comp->exit_code);
        fprintf(fp, "    }%s\n", (i < count - 1) ? "," : "");
    }
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);

    // Cleanup sorted arrays
    if (slowest) {
        for (size_t i = 0; i < slowest_count; i++) free(slowest[i].component_name);
        free(slowest);
    }
    if (most_cpu) {
        for (size_t i = 0; i < cpu_count; i++) free(most_cpu[i].component_name);
        free(most_cpu);
    }
    if (most_memory) {
        for (size_t i = 0; i < memory_count; i++) free(most_memory[i].component_name);
        free(most_memory);
    }
    if (most_io) {
        for (size_t i = 0; i < io_count; i++) free(most_io[i].component_name);
        free(most_io);
    }
}

// Execute children following infinite index pattern with benchmarking
int execute_children(orchestrator_t* orch, component_benchmark_t** benchmarks_out, size_t* benchmark_count_out) {
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

    // Initialize benchmark array
    component_benchmark_t* benchmarks = calloc(num_children, sizeof(component_benchmark_t));
    if (!benchmarks) {
        fprintf(stderr, "Error: Could not allocate benchmark array\n");
        for (size_t i = 0; i < num_children; i++) free(children[i]);
        free(children);
        return 1;
    }
    size_t benchmark_count = 0;

    for (size_t i = 0; i < num_children; i++) {
        const char* child_name = children[i];
        char child_cmd[1024];

        // Try different executable naming patterns
        // Pattern 1: {child_name}/{child_name} (e.g., hello/hello)
        snprintf(child_cmd, sizeof(child_cmd), "%s/%s/%s", orch->module_path, child_name, child_name);

        if (access(child_cmd, X_OK) == 0) {
            log_state(orch, "Executing child: %s (pattern 1: %s)", child_name, child_cmd);
            printf("Executing child: %s\n", child_name);

            // Collect timing for both high-precision and legacy compatibility
            struct timespec start_wall_time;
            clock_gettime(CLOCK_MONOTONIC, &start_wall_time);
            time_t start_time = time(NULL);
            benchmark_metrics_t before_metrics = get_current_metrics();

            int result = system(child_cmd);

            // Collect metrics after execution
            struct timespec end_wall_time;
            clock_gettime(CLOCK_MONOTONIC, &end_wall_time);
            time_t end_time = time(NULL);
            benchmark_metrics_t after_metrics = get_current_metrics();

            // Calculate wall time delta
            double wall_time_delta = (end_wall_time.tv_sec - start_wall_time.tv_sec) +
                                   (end_wall_time.tv_nsec - start_wall_time.tv_nsec) / 1e9;

            // Calculate metric deltas
            benchmark_metrics_t delta = calculate_delta(before_metrics, after_metrics);
            delta.wall_time_sec = wall_time_delta;

            // Store benchmark data
            if (benchmark_count < num_children) {
                benchmarks[benchmark_count].component_name = strdup(child_name);
                benchmarks[benchmark_count].metrics = delta;
                benchmarks[benchmark_count].exit_code = result;
                benchmark_count++;
            }

            if (result != 0) {
                log_state(orch, "WARNING: Child '%s' exited with code %d (took %.3f seconds)", child_name, result, wall_time_delta);
                fprintf(stderr, "Warning: Child '%s' exited with code %d\n", child_name, result);
            } else {
                log_state(orch, "SUCCESS: Child '%s' completed successfully (took %.3f seconds)", child_name, wall_time_delta);
            }

            // Note: Children now write to centralized state.json
            // No need to read .report files anymore
            add_child_state(orch, child_name, result, start_time, end_time, NULL);
            continue;
        }

        // Pattern 2: {child_name}/index (for Python-style modules)
        snprintf(child_cmd, sizeof(child_cmd), "%s/%s/index", orch->module_path, child_name);

        if (access(child_cmd, X_OK) == 0) {
            log_state(orch, "Executing child: %s (pattern 2: %s)", child_name, child_cmd);
            printf("Executing child: %s (index)\n", child_name);

            // Collect timing for both high-precision and legacy compatibility
            struct timespec start_wall_time;
            clock_gettime(CLOCK_MONOTONIC, &start_wall_time);
            time_t start_time = time(NULL);
            benchmark_metrics_t before_metrics = get_current_metrics();

            int result = system(child_cmd);

            // Collect metrics after execution
            struct timespec end_wall_time;
            clock_gettime(CLOCK_MONOTONIC, &end_wall_time);
            time_t end_time = time(NULL);
            benchmark_metrics_t after_metrics = get_current_metrics();

            // Calculate wall time delta
            double wall_time_delta = (end_wall_time.tv_sec - start_wall_time.tv_sec) +
                                   (end_wall_time.tv_nsec - start_wall_time.tv_nsec) / 1e9;

            // Calculate metric deltas
            benchmark_metrics_t delta = calculate_delta(before_metrics, after_metrics);
            delta.wall_time_sec = wall_time_delta;

            // Store benchmark data
            if (benchmark_count < num_children) {
                benchmarks[benchmark_count].component_name = strdup(child_name);
                benchmarks[benchmark_count].metrics = delta;
                benchmarks[benchmark_count].exit_code = result;
                benchmark_count++;
            }

            if (result != 0) {
                log_state(orch, "WARNING: Child '%s' exited with code %d (took %.3f seconds)", child_name, result, wall_time_delta);
                fprintf(stderr, "Warning: Child '%s' exited with code %d\n", child_name, result);
            } else {
                log_state(orch, "SUCCESS: Child '%s' completed successfully (took %.3f seconds)", child_name, wall_time_delta);
            }

            // Note: Children now write to centralized state.json
            // No need to read .report files anymore
            add_child_state(orch, child_name, result, start_time, end_time, NULL);
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

    // Set output parameters
    *benchmarks_out = benchmarks;
    *benchmark_count_out = benchmark_count;

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

    // Parse command line arguments
    char* committed_not_pushed_mode = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--committed-not-pushed-tree") == 0) {
            committed_not_pushed_mode = "tree";
        } else if (strcmp(argv[i], "--committed-not-pushed-flat") == 0) {
            committed_not_pushed_mode = "flat";
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --committed-not-pushed-tree    Display committed-not-pushed in tree mode\n");
            printf("  --committed-not-pushed-flat    Display committed-not-pushed in flat mode\n");
            printf("  --help, -h                     Show this help message\n");
            return 0;
        }
    }

    // Set environment variable for committed-not-pushed mode if specified
    if (committed_not_pushed_mode) {
        char env_var[256];
        snprintf(env_var, sizeof(env_var), "COMMITTED_NOT_PUSHED_MODE=%s", committed_not_pushed_mode);
        putenv(strdup(env_var));
    }

    // Initialize orchestrator (infinite index pattern)
    orchestrator_t* orch = orchestrator_init(module_path);
    if (!orch) {
        fprintf(stderr, "Error: Failed to initialize orchestrator\n");
        return 1;
    }

    // Execute children with benchmarking
    component_benchmark_t* benchmarks = NULL;
    size_t benchmark_count = 0;
    int result = execute_children(orch, &benchmarks, &benchmark_count);

    // Set session end time
    orch->state.session_end = time(NULL);

    log_state(orch, "Child execution phase completed with result: %d", result);

    // Generate benchmark report if we have data
    if (benchmarks && benchmark_count > 0) {
        write_benchmark_report(orch, benchmarks, benchmark_count);
        log_state(orch, "Generated benchmark report with %zu component measurements", benchmark_count);

        // Cleanup benchmark data
        for (size_t i = 0; i < benchmark_count; i++) {
            free(benchmarks[i].component_name);
        }
        free(benchmarks);
    }

    // Display child reports before main loop - DISABLED by user request
    // display_child_reports(orch);

    log_state(orch, "Starting main application loop");

    // Run main application loop
    // COMMENTED OUT: Perpetual while loop disabled for rapid testing
    /*
    if (result == 0) {
        result = run_main_loop();
        log_state(orch, "Main application loop exited with result: %d", result);
    } else {
        log_state(orch, "Skipping main loop due to child execution failure");
    }
    */

    // Cleanup
    orchestrator_cleanup(orch);

    return result;
}