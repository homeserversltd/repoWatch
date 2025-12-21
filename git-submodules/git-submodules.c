#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <regex.h>
#include <dirent.h>
#include "../json-utils/json-utils.h"

// Configuration structure
typedef struct {
    char* repo_path;
    char* status_cache;
    int max_depth;
    int check_interval;
    int cache_status;
    int report_changes_only;
    int include_parent_status;
} config_t;

// Repository status structure
typedef struct {
    char* path;
    char* name;
    char* status;
    int is_clean;
    time_t last_check;
} repo_status_t;

// Status collection structure
typedef struct {
    repo_status_t* repos;
    size_t count;
    size_t capacity;
} status_collection_t;

// Custom environment variable expansion
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

// Load configuration from index.json and environment variables
config_t* load_config() {
    config_t* config = calloc(1, sizeof(config_t));
    if (!config) return NULL;

    // Load index.json configuration
    json_value_t* root = index_json_load(".");
    if (root && root->type == JSON_OBJECT) {
        // Get repo path from paths.repo_path
        json_value_t* repo_path_val = get_nested_value(root, "paths.repo_path");
        if (repo_path_val && repo_path_val->type == JSON_STRING) {
            config->repo_path = strdup(repo_path_val->value.str_val);
        } else {
            config->repo_path = strdup("/home/owner/git/serverGenesis");
        }

        // Get cache path from paths.status_cache
        json_value_t* cache_path_val = get_nested_value(root, "paths.status_cache");
        if (cache_path_val && cache_path_val->type == JSON_STRING) {
            config->status_cache = expandvars(cache_path_val->value.str_val);
        } else {
            char* cache_path = expandvars("${XDG_CACHE_HOME:-~/.cache}/repowatch/git-submodules.cache");
            config->status_cache = cache_path ? cache_path : strdup("/tmp/git-submodules.cache");
        }

        // Get max_depth from config.max_depth
        json_value_t* max_depth_val = get_nested_value(root, "config.max_depth");
        if (max_depth_val && max_depth_val->type == JSON_NUMBER) {
            config->max_depth = (int)max_depth_val->value.num_val;
        } else {
            config->max_depth = 3; // Default fallback
        }

        // Get check_interval from config.check_interval
        json_value_t* check_interval_val = get_nested_value(root, "config.check_interval");
        if (check_interval_val && check_interval_val->type == JSON_NUMBER) {
            config->check_interval = (int)check_interval_val->value.num_val;
        } else {
            config->check_interval = 1; // Default fallback
        }

        // Get cache_status from config.cache_status
        json_value_t* cache_status_val = get_nested_value(root, "config.cache_status");
        if (cache_status_val && cache_status_val->type == JSON_BOOL) {
            config->cache_status = cache_status_val->value.bool_val;
        } else {
            config->cache_status = 1; // Default fallback
        }

        // Get report_changes_only from config.report_changes_only
        json_value_t* report_changes_val = get_nested_value(root, "config.report_changes_only");
        if (report_changes_val && report_changes_val->type == JSON_BOOL) {
            config->report_changes_only = report_changes_val->value.bool_val;
        } else {
            config->report_changes_only = 1; // Default fallback
        }

        // Get include_parent_status from config.include_parent_status
        json_value_t* include_parent_val = get_nested_value(root, "config.include_parent_status");
        if (include_parent_val && include_parent_val->type == JSON_BOOL) {
            config->include_parent_status = include_parent_val->value.bool_val;
        } else {
            config->include_parent_status = 1; // Default fallback
        }

        json_free(root);
    } else {
        // Fallback to hardcoded defaults if JSON loading fails
        config->repo_path = strdup("/home/owner/git/serverGenesis");
        char* cache_path = expandvars("${XDG_CACHE_HOME:-~/.cache}/repowatch/git-submodules.cache");
        config->status_cache = cache_path ? cache_path : strdup("/tmp/git-submodules.cache");
        config->max_depth = 3;
        config->check_interval = 1;
        config->cache_status = 1;
        config->report_changes_only = 1;
        config->include_parent_status = 1;
    }

    return config;
}

// Initialize status collection
status_collection_t* status_collection_init() {
    status_collection_t* collection = calloc(1, sizeof(status_collection_t));
    if (!collection) return NULL;

    collection->capacity = 16;
    collection->repos = calloc(collection->capacity, sizeof(repo_status_t));
    if (!collection->repos) {
        free(collection);
        return NULL;
    }

    return collection;
}

// Add repository status to collection
void add_repo_status(status_collection_t* collection, const char* path, const char* name, const char* status, int is_clean) {
    if (collection->count >= collection->capacity) {
        collection->capacity *= 2;
        repo_status_t* new_repos = realloc(collection->repos, collection->capacity * sizeof(repo_status_t));
        if (!new_repos) return;
        collection->repos = new_repos;
    }

    repo_status_t* repo = &collection->repos[collection->count];
    repo->path = strdup(path);
    repo->name = strdup(name);
    repo->status = status ? strdup(status) : NULL;
    repo->is_clean = is_clean;
    repo->last_check = time(NULL);

    collection->count++;
}

// Get git status for a specific repository
char* get_git_status(const char* repo_path) {
    char cmd[2048];
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

// Check if directory is a git repository
int is_git_repo(const char* path) {
    char git_dir[2048];
    snprintf(git_dir, sizeof(git_dir), "%s/.git", path);
    return access(git_dir, F_OK) == 0;
}

// Read git submodules from .gitmodules file
char** read_gitmodules(const char* repo_path, size_t* count) {
    char gitmodules_path[2048];
    snprintf(gitmodules_path, sizeof(gitmodules_path), "%s/.gitmodules", repo_path);

    FILE* fp = fopen(gitmodules_path, "r");
    if (!fp) {
        *count = 0;
        return NULL;
    }

    // Count submodules first
    *count = 0;
    char line[1024];
    regex_t path_regex;
    regcomp(&path_regex, "path = (.+)", REG_EXTENDED);

    while (fgets(line, sizeof(line), fp) != NULL) {
        regmatch_t matches[2];
        if (regexec(&path_regex, line, 2, matches, 0) == 0) {
            (*count)++;
        }
    }

    if (*count == 0) {
        regfree(&path_regex);
        fclose(fp);
        return NULL;
    }

    // Allocate array for paths
    char** paths = calloc(*count, sizeof(char*));
    if (!paths) {
        regfree(&path_regex);
        fclose(fp);
        *count = 0;
        return NULL;
    }

    // Read paths
    rewind(fp);
    size_t index = 0;
    while (fgets(line, sizeof(line), fp) != NULL && index < *count) {
        regmatch_t matches[2];
        if (regexec(&path_regex, line, 2, matches, 0) == 0) {
            size_t len = matches[1].rm_eo - matches[1].rm_so;
            char* path = malloc(len + 1);
            if (path) {
                strncpy(path, line + matches[1].rm_so, len);
                path[len] = '\0';
                // Remove trailing whitespace
                char* end = path + strlen(path) - 1;
                while (end > path && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) {
                    *end-- = '\0';
                }
                paths[index++] = path;
            }
        }
    }

    regfree(&path_regex);
    fclose(fp);
    *count = index; // Update count in case we found fewer than expected

    return paths;
}

// Recursively collect git status for repository and submodules
void collect_repo_status(status_collection_t* collection, const char* repo_path, const char* repo_name, int current_depth, int max_depth) {
    if (current_depth > max_depth) return;

    // Check if this is a git repository
    if (!is_git_repo(repo_path)) return;

    // Get git status for this repository
    char* status = get_git_status(repo_path);
    int is_clean = (!status || strlen(status) == 0);

    // Add to collection
    add_repo_status(collection, repo_path, repo_name, status, is_clean);
    free(status);

    // Always check submodules if they exist
    size_t submodule_count = 0;
    char** submodule_paths = read_gitmodules(repo_path, &submodule_count);

    if (submodule_paths) {
        for (size_t i = 0; i < submodule_count; i++) {
            char submodule_full_path[2048];
            char submodule_name[256];

            // Construct full path to submodule
            snprintf(submodule_full_path, sizeof(submodule_full_path), "%s/%s", repo_path, submodule_paths[i]);

            // Extract just the directory name for the submodule name
            char* last_slash = strrchr(submodule_paths[i], '/');
            if (last_slash) {
                strcpy(submodule_name, last_slash + 1);
            } else {
                strcpy(submodule_name, submodule_paths[i]);
            }

            // Recursively collect status for this submodule
            collect_repo_status(collection, submodule_full_path, submodule_name, current_depth + 1, max_depth);

            free(submodule_paths[i]);
        }
        free(submodule_paths);
    }
}

// Generate JSON report file
void generate_json_report(status_collection_t* collection, const char* repo_path) {
    // Create root JSON object
    json_value_t* root = json_create_object();
    if (!root) {
        fprintf(stderr, "Failed to create JSON root object\n");
        return;
    }

    // Add metadata
    json_object_set(root, "report_type", json_create_string("git_submodules_status"));
    json_object_set(root, "root_repository", json_create_string(repo_path));
    json_object_set(root, "timestamp", json_create_number((double)time(NULL)));
    json_object_set(root, "total_repositories_checked", json_create_number((double)collection->count));

    // Create repositories array
    json_value_t* repos_array = json_create_array();
    if (!repos_array) {
        json_free(root);
        return;
    }

    int total_clean = 0;
    int total_dirty = 0;

    for (size_t i = 0; i < collection->count; i++) {
        repo_status_t* repo = &collection->repos[i];

        // Create repository object
        json_value_t* repo_obj = json_create_object();
        if (!repo_obj) continue;

        json_object_set(repo_obj, "name", json_create_string(repo->name));
        json_object_set(repo_obj, "path", json_create_string(repo->path));
        json_object_set(repo_obj, "is_clean", json_create_bool(repo->is_clean));
        json_object_set(repo_obj, "last_check", json_create_number((double)repo->last_check));

        if (repo->is_clean) {
            total_clean++;
            json_object_set(repo_obj, "status", json_create_string("CLEAN"));
            json_object_set(repo_obj, "changes", json_create_string(""));
        } else {
            total_dirty++;
            json_object_set(repo_obj, "status", json_create_string("DIRTY"));
            json_object_set(repo_obj, "changes", json_create_string(repo->status ? repo->status : ""));
        }

        json_array_add(repos_array, repo_obj);
    }

    // Add repositories array to root
    json_object_set(root, "repositories", repos_array);

    // Create summary object
    json_value_t* summary = json_create_object();
    if (summary) {
        json_object_set(summary, "clean_repositories", json_create_number((double)total_clean));
        json_object_set(summary, "dirty_repositories", json_create_number((double)total_dirty));
        json_object_set(root, "summary", summary);
    }

    // Write JSON to file
    if (json_write_file("git-submodules.report", root) != 0) {
        fprintf(stderr, "Failed to write JSON report file\n");
    }

    json_free(root);
}

// Cleanup status collection
void status_collection_cleanup(status_collection_t* collection) {
    if (collection) {
        for (size_t i = 0; i < collection->count; i++) {
            free(collection->repos[i].path);
            free(collection->repos[i].name);
            free(collection->repos[i].status);
        }
        free(collection->repos);
        free(collection);
    }
}

int main(int argc, char* argv[]) {
    printf("Git Submodules Monitor starting...\n");

    config_t* config = load_config();
    if (!config) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }

    printf("Monitoring repository: %s (max depth: %d)\n", config->repo_path, config->max_depth);

    // Initialize status collection
    status_collection_t* collection = status_collection_init();
    if (!collection) {
        fprintf(stderr, "Failed to initialize status collection\n");
        free(config->repo_path);
        free(config->status_cache);
        free(config);
        return 1;
    }

    // Collect status for root repository and all submodules
    collect_repo_status(collection, config->repo_path, "root", 0, config->max_depth);

    printf("Checked %zu repositories\n", collection->count);

    // Generate JSON report
    generate_json_report(collection, config->repo_path);
    printf("Report generated\n");

    // Print summary to stdout
    int clean_count = 0;
    int dirty_count = 0;

    for (size_t i = 0; i < collection->count; i++) {
        if (collection->repos[i].is_clean) {
            clean_count++;
        } else {
            dirty_count++;
        }
    }

    printf("Repository status summary:\n");
    printf("  Clean: %d\n", clean_count);
    printf("  Dirty: %d\n", dirty_count);

    if (dirty_count > 0) {
        printf("Dirty repositories:\n");
        for (size_t i = 0; i < collection->count; i++) {
            if (!collection->repos[i].is_clean) {
                printf("  - %s (%s)\n", collection->repos[i].name, collection->repos[i].path);
            }
        }
    }

    // Cleanup
    status_collection_cleanup(collection);
    free(config->repo_path);
    free(config->status_cache);
    free(config);

    printf("Git Submodules Monitor completed\n");
    return 0;
}
