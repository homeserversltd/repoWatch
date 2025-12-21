#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <regex.h>

// Configuration structure
typedef struct {
    char* repo_path;
    char* status_cache;
    int check_interval;
    int cache_status;
    int report_changes_only;
} config_t;

// Load configuration from environment variables
config_t* load_config() {
    config_t* config = calloc(1, sizeof(config_t));
    if (!config) return NULL;

    // Use environment variables or defaults
    const char* repo_env = getenv("REPO_WATCH_REPO_PATH");
    config->repo_path = repo_env ? strdup(repo_env) : strdup(".");
    // Simple cache path for now
    config->status_cache = strdup("/tmp/git-status.cache");

    config->check_interval = 1;
    config->cache_status = 1;
    config->report_changes_only = 1;

    return config;
}

// Get git status output
char* get_git_status(const char* repo_path) {
    char cmd[1024];
    FILE* fp;
    char* status_output = NULL;
    size_t size = 0;

    // Change to repo directory and run git status --porcelain
    snprintf(cmd, sizeof(cmd), "cd '%s' && git status --porcelain 2>/dev/null", repo_path);

    fp = popen(cmd, "r");
    if (!fp) return strdup("");

    // Read all output
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        size_t len = strlen(buffer);
        char* new_output = realloc(status_output, size + len + 1);
        if (!new_output) {
            free(status_output);
            pclose(fp);
            return NULL;
        }
        status_output = new_output;
        strcpy(status_output + size, buffer);
        size += len;
    }

    pclose(fp);

    // Null terminate if we have content
    if (status_output) {
        status_output[size] = '\0';
    }

    return status_output ?: strdup("");
}

// Read cached status
char* read_cached_status(const char* cache_file) {
    FILE* fp = fopen(cache_file, "r");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);

    return content;
}

// Write status to cache
void write_cached_status(const char* cache_file, const char* status) {
    FILE* fp = fopen(cache_file, "w");
    if (fp) {
        fprintf(fp, "%s", status ?: "");
        fclose(fp);
    }
}

// Compare statuses and determine if changed
int status_changed(const char* current, const char* cached) {
    if (!current && !cached) return 0;
    if (!current || !cached) return 1;
    return strcmp(current, cached) != 0;
}

// Generate report
void generate_report(const char* current_status, int changed, const char* repo_path) {
    FILE* report_file = fopen("git-status/.report", "w");
    if (!report_file) {
        fprintf(stderr, "Failed to create report file\n");
        return;
    }

    fprintf(report_file, "Git Status Report\n");
    fprintf(report_file, "=================\n");
    fprintf(report_file, "Repository: %s\n", repo_path);
    fprintf(report_file, "Status Changed: %s\n", changed ? "YES" : "NO");
    fprintf(report_file, "Timestamp: %ld\n", time(NULL));

    if (current_status && strlen(current_status) > 0) {
        fprintf(report_file, "\nCurrent Status:\n%s", current_status);
    } else {
        fprintf(report_file, "\nCurrent Status: Clean (no changes)\n");
    }

    fclose(report_file);
}

int main(int argc, char* argv[]) {
    printf("Git Status Monitor starting...\n");

    config_t* config = load_config();
    if (!config) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }

    printf("Monitoring repository: %s\n", config->repo_path);

    // Get current git status
    char* current_status = get_git_status(config->repo_path);
    if (!current_status) {
        fprintf(stderr, "Failed to get git status\n");
        free(config->repo_path);
        free(config->status_cache);
        free(config);
        return 1;
    }

    printf("Retrieved git status (%zu bytes)\n", strlen(current_status));

    // Check if status changed
    int changed = 0;
    if (config->cache_status) {
        char* cached_status = read_cached_status(config->status_cache);
        changed = status_changed(current_status, cached_status);
        free(cached_status);
    } else {
        changed = 1; // Always report if not caching
    }

    // Generate report
    generate_report(current_status, changed, config->repo_path);
    printf("Report generated\n");

    // Update cache if configured
    if (config->cache_status) {
        write_cached_status(config->status_cache, current_status);
        printf("Status cached\n");
    }

    // Print summary to stdout
    if (changed || !config->report_changes_only) {
        printf("Git status %s\n", changed ? "CHANGED" : "UNCHANGED");
        if (current_status && strlen(current_status) > 0) {
            printf("Changes detected:\n%s", current_status);
        } else {
            printf("Repository is clean\n");
        }
    }

    // Cleanup
    free(current_status);
    free(config->repo_path);
    free(config->status_cache);
    free(config);

    printf("Git Status Monitor completed\n");
    return 0;
}
