#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <time.h>
#include "../json-utils/json-utils.h"

// View mode enumeration
typedef enum {
    VIEW_FLAT,
    VIEW_TREE
} view_mode_t;

// Configuration for committed-not-pushed module
typedef struct {
    char* repo_path;
    int max_commit_count;
    int show_commit_hashes;
    int include_branch_info;
    char* display_mode;
    char* tree_prefix;
    char* tree_last_prefix;
    char* tree_indent;
    int max_display_files;
    view_mode_t current_view;
} committed_not_pushed_config_t;

// Forward declarations
char* get_nested_string(json_value_t* root, const char* key_path, const char* default_value);
int get_nested_int(json_value_t* root, const char* key_path, int default_value);
char* expandvars(const char* input);
char* truncate_filename(const char* filename, int is_file);
json_value_t* get_nested_value(json_value_t* root, const char* key_path);

// Load configuration from index.json
committed_not_pushed_config_t* load_config(const char* module_path) {
    // When run as child process, module_path is repoWatch root, so look for committed-not-pushed/index.json
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/committed-not-pushed/index.json", module_path);

    printf("DEBUG: Looking for config at: %s\n", index_path);
    json_value_t* config_json = json_parse_file(index_path);
    if (!config_json) {
        fprintf(stderr, "Failed to load configuration from %s\n", index_path);
        return NULL;
    }

    committed_not_pushed_config_t* config = calloc(1, sizeof(committed_not_pushed_config_t));
    if (!config) {
        json_free(config_json);
        return NULL;
    }

    // Load config values with defaults
    config->repo_path = expandvars(get_nested_string(config_json, "paths.repo_path", "/home/owner/git/serverGenesis"));
    config->max_commit_count = get_nested_int(config_json, "config.max_commit_count", 50);
    config->show_commit_hashes = get_nested_int(config_json, "config.show_commit_hashes", 1);
    config->include_branch_info = get_nested_int(config_json, "config.include_branch_info", 1);

    char* raw_display_mode = get_nested_string(config_json, "config.display_mode", "flat");
    printf("DEBUG: Raw display_mode from JSON: '%s'\n", raw_display_mode ? raw_display_mode : "NULL");
    config->display_mode = expandvars(raw_display_mode);
    config->tree_prefix = expandvars(get_nested_string(config_json, "config.tree_prefix", "├── "));
    config->tree_last_prefix = expandvars(get_nested_string(config_json, "config.tree_last_prefix", "└── "));
    config->tree_indent = expandvars(get_nested_string(config_json, "config.tree_indent", "│   "));
    config->max_display_files = get_nested_int(config_json, "config.max_display_files", 50);

    // Check for environment variable override
    char* env_mode = getenv("COMMITTED_NOT_PUSHED_MODE");
    if (env_mode) {
        free(config->display_mode);
        config->display_mode = strdup(env_mode);
    }

    // Set view mode based on display_mode string
    if (strcmp(config->display_mode, "tree") == 0) {
        config->current_view = VIEW_TREE;
    } else {
        config->current_view = VIEW_FLAT;
    }

    // Debug output
    printf("DEBUG: Loaded display_mode: '%s', current_view: %s\n",
           config->display_mode,
           config->current_view == VIEW_TREE ? "TREE" : "FLAT");

    json_free(config_json);
    return config;
}


// Helper function to get string value from nested JSON path
char* get_nested_string(json_value_t* root, const char* key_path, const char* default_value) {
    json_value_t* value = get_nested_value(root, key_path);
    if (value && value->type == JSON_STRING) {
        return strdup(value->value.str_val);
    }
    return default_value ? strdup(default_value) : NULL;
}

// Helper function to get int value from nested JSON path
int get_nested_int(json_value_t* root, const char* key_path, int default_value) {
    json_value_t* value = get_nested_value(root, key_path);
    if (value && value->type == JSON_NUMBER) {
        return (int)value->value.num_val;
    }
    return default_value;
}

// Custom environment variable expansion (simple version)
char* expandvars(const char* input) {
    if (!input) return NULL;
    // For now, just return a copy. Could be enhanced to handle ${VAR} syntax
    return strdup(input);
}


// Helper function to get display repo name (similar to interactive-dirty-files-tui)
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

// Structure for committed-not-pushed information
typedef struct {
    char* repo_path;
    char* repo_name;
    char** unpushed_commits;
    size_t commit_count;
    size_t commit_capacity;
    char*** commit_files;  // Array of file arrays for each commit
    size_t* commit_file_counts;  // Number of files for each commit
} unpushed_repo_t;

// Collection of repositories with unpushed commits
typedef struct {
    unpushed_repo_t* repos;
    size_t count;
    size_t capacity;
    char** submodule_paths;  // List of submodule paths to filter out
    size_t submodule_count;
} unpushed_collection_t;

// Display results in flat format (current behavior)
void display_flat_view(unpushed_collection_t* collection, committed_not_pushed_config_t* config) {
    size_t total_unpushed_repos = 0;
    size_t total_unpushed_commits = 0;

    // Calculate totals
    for (size_t i = 0; i < collection->count; i++) {
        unpushed_repo_t* repo = &collection->repos[i];
        if (repo->commit_count > 0) {
            total_unpushed_repos++;
            total_unpushed_commits += repo->commit_count;
        }
    }

    printf("\nCommitted Not Pushed Analysis Summary:\n");
    printf("  Total repositories with unpushed commits: %zu\n", total_unpushed_repos);
    printf("  Total unpushed commits: %zu\n", total_unpushed_commits);

    if (total_unpushed_repos > 0) {
        printf("\nDetailed breakdown:\n");
        for (size_t i = 0; i < collection->count; i++) {
            unpushed_repo_t* repo = &collection->repos[i];
            if (repo->commit_count > 0) {
                printf("  %s (%s): %zu unpushed commits\n",
                       repo->repo_name, repo->repo_path, repo->commit_count);
                for (size_t j = 0; j < repo->commit_count && j < 2; j++) {  // Show first 2 commits
                    printf("    - %s\n", repo->unpushed_commits[j]);

                    // Show files changed in this commit (up to 5 files)
                    if (repo->commit_file_counts[j] > 0) {
                        printf("      Files changed:\n");
                        size_t max_files = repo->commit_file_counts[j] < 5 ? repo->commit_file_counts[j] : 5;
                        for (size_t k = 0; k < max_files; k++) {
                            char* truncated_file = truncate_filename(repo->commit_files[j][k], 1); // 1 = is_file
                            printf("        • %s\n", truncated_file);
                            free(truncated_file);
                        }
                        if (repo->commit_file_counts[j] > 5) {
                            printf("        ... and %zu more files\n", repo->commit_file_counts[j] - 5);
                        }
                    }
                }
                if (repo->commit_count > 2) {
                    printf("    ... and %zu more commits\n", repo->commit_count - 2);
                }
            }
        }
    }
}

// Display results in tree format (similar to interactive-dirty-files-tui)
void display_tree_view(unpushed_collection_t* collection, committed_not_pushed_config_t* config) {
    size_t total_unpushed_repos = 0;
    size_t total_unpushed_commits = 0;

    // Calculate totals
    for (size_t i = 0; i < collection->count; i++) {
        unpushed_repo_t* repo = &collection->repos[i];
        if (repo->commit_count > 0) {
            total_unpushed_repos++;
            total_unpushed_commits += repo->commit_count;
        }
    }

    // Title
    printf("Committed Not Pushed Analysis (TREE)\n");

    // Summary line
    printf("Total: %zu repos with unpushed commits, %zu unpushed commits\n",
           total_unpushed_repos, total_unpushed_commits);

    // Display repositories as trees
    for (size_t i = 0; i < collection->count; i++) {
        unpushed_repo_t* repo = &collection->repos[i];
        if (repo->commit_count > 0) {
            // Repository header
            const char* display_name = get_display_repo_name(repo->repo_name, repo->repo_path);
            printf("\nRepository: %s\n", display_name);

            // Display commits as tree
            for (size_t j = 0; j < repo->commit_count; j++) {
                int is_last_commit = (j == repo->commit_count - 1);

                // Print tree prefix for commit
                if (is_last_commit) {
                    printf("%s", config->tree_last_prefix);
                } else {
                    printf("%s", config->tree_prefix);
                }

                // Print commit info (truncated if too long)
                char commit_line[256];
                size_t commit_len = strlen(repo->unpushed_commits[j]);
                if (commit_len > 60) {
                    strncpy(commit_line, repo->unpushed_commits[j], 57);
                    commit_line[57] = '\0';
                    strcat(commit_line, "...");
                } else {
                    strcpy(commit_line, repo->unpushed_commits[j]);
                }
                printf("%s\n", commit_line);

                // Display files changed in this commit
                if (repo->commit_file_counts[j] > 0) {
                    size_t max_files = repo->commit_file_counts[j] < config->max_display_files ?
                                     repo->commit_file_counts[j] : config->max_display_files;

                    for (size_t k = 0; k < max_files; k++) {
                        int is_last_file = (k == max_files - 1) && (repo->commit_file_counts[j] <= config->max_display_files);

                        // Print indentation
                        if (is_last_commit) {
                            printf("    ");
                        } else {
                            printf("%s", config->tree_indent);
                        }

                        // Print file tree prefix
                        if (is_last_file) {
                            printf("%s", config->tree_last_prefix);
                        } else {
                            printf("%s", config->tree_prefix);
                        }

                        // Print file name (truncated if necessary)
                        char* truncated_file = truncate_filename(repo->commit_files[j][k], 1);
                        printf("%s\n", truncated_file);
                        free(truncated_file);
                    }

                    if (repo->commit_file_counts[j] > config->max_display_files) {
                        // Print continuation indicator
                        if (is_last_commit) {
                            printf("    %s... and %zu more files\n",
                                   config->tree_last_prefix,
                                   repo->commit_file_counts[j] - config->max_display_files);
                        } else {
                            printf("%s%s... and %zu more files\n",
                                   config->tree_indent, config->tree_last_prefix,
                                   repo->commit_file_counts[j] - config->max_display_files);
                        }
                    }
                }
            }
        }
    }
}

// Initialize unpushed collection
unpushed_collection_t* unpushed_collection_init() {
    unpushed_collection_t* collection = calloc(1, sizeof(unpushed_collection_t));
    if (!collection) return NULL;

    collection->capacity = 8;
    collection->repos = calloc(collection->capacity, sizeof(unpushed_repo_t));
    if (!collection->repos) {
        free(collection);
        return NULL;
    }

    collection->submodule_paths = NULL;
    collection->submodule_count = 0;

    return collection;
}

// Check if a path is a submodule
int is_submodule_path(unpushed_collection_t* collection, const char* path) {
    for (size_t i = 0; i < collection->submodule_count; i++) {
        if (strcmp(collection->submodule_paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

// Add submodule path to filter list
void add_submodule_path(unpushed_collection_t* collection, const char* path) {
    collection->submodule_paths = realloc(collection->submodule_paths,
                                         (collection->submodule_count + 1) * sizeof(char*));
    collection->submodule_paths[collection->submodule_count] = strdup(path);
    collection->submodule_count++;
}

// Add repository to collection
void add_repo(unpushed_collection_t* collection, const char* repo_path, const char* repo_name) {
    if (collection->count >= collection->capacity) {
        collection->capacity *= 2;
        collection->repos = realloc(collection->repos,
                                   collection->capacity * sizeof(unpushed_repo_t));
    }

    unpushed_repo_t* repo = &collection->repos[collection->count];
    repo->repo_path = strdup(repo_path);
    repo->repo_name = strdup(repo_name);
    repo->unpushed_commits = NULL;
    repo->commit_count = 0;
    repo->commit_capacity = 8;
    repo->unpushed_commits = calloc(repo->commit_capacity, sizeof(char*));
    repo->commit_files = calloc(repo->commit_capacity, sizeof(char**));
    repo->commit_file_counts = calloc(repo->commit_capacity, sizeof(size_t));

    collection->count++;
}

// Add unpushed commit to repository
void add_unpushed_commit(unpushed_repo_t* repo, const char* commit_info) {
    if (repo->commit_count >= repo->commit_capacity) {
        repo->commit_capacity *= 2;
        repo->unpushed_commits = realloc(repo->unpushed_commits,
                                        repo->commit_capacity * sizeof(char*));
        repo->commit_files = realloc(repo->commit_files,
                                    repo->commit_capacity * sizeof(char**));
        repo->commit_file_counts = realloc(repo->commit_file_counts,
                                          repo->commit_capacity * sizeof(size_t));
    }

    repo->unpushed_commits[repo->commit_count] = strdup(commit_info);
    repo->commit_files[repo->commit_count] = NULL;
    repo->commit_file_counts[repo->commit_count] = 0;
    repo->commit_count++;
}

// Don't truncate filenames - let the UI layer handle truncation
char* truncate_filename(const char* filename, int is_file) {
    // Return filename as-is - UI will handle truncation with glyph-aware logic
    return strdup(filename);
}

// Get files changed in a specific commit
void get_commit_files(unpushed_repo_t* repo, size_t commit_index) {
    char commit_hash[9]; // First 8 chars of commit hash + null
    const char* commit_line = repo->unpushed_commits[commit_index];

    // Extract commit hash (first 8 characters before space)
    size_t hash_len = 0;
    while (commit_line[hash_len] && commit_line[hash_len] != ' ' && hash_len < 8) {
        commit_hash[hash_len] = commit_line[hash_len];
        hash_len++;
    }
    commit_hash[hash_len] = '\0';

    // Run git show --name-only <commit> to get files changed
    char cmd[2048];
    FILE* fp;

    snprintf(cmd, sizeof(cmd), "cd '%s' && git show --name-only --pretty=format: %s 2>/dev/null",
             repo->repo_path, commit_hash);

    fp = popen(cmd, "r");
    if (!fp) return;

    char buffer[1024];
    size_t file_count = 0;
    size_t file_capacity = 8;
    char** files = calloc(file_capacity, sizeof(char*));

    // Skip the first line (commit message if any)
    if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // First line might be empty or commit info, skip it
    }

    // Read file names
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Remove newline
        buffer[strcspn(buffer, "\n")] = 0;

        // Skip empty lines
        if (strlen(buffer) == 0) continue;

        // Add file to list
        if (file_count >= file_capacity) {
            file_capacity *= 2;
            files = realloc(files, file_capacity * sizeof(char*));
        }
        files[file_count] = strdup(buffer);
        file_count++;
    }

    pclose(fp);

    // Store files for this commit
    repo->commit_files[commit_index] = files;
    repo->commit_file_counts[commit_index] = file_count;
}

// Get unpushed commits for a specific repository
void get_unpushed_commits(unpushed_collection_t* collection, unpushed_repo_t* repo) {
    char cmd[2048];
    FILE* fp;
    char buffer[1024];

    // First, check if repository has a remote
    snprintf(cmd, sizeof(cmd), "cd '%s' && git remote 2>/dev/null", repo->repo_path);
    fp = popen(cmd, "r");
    if (!fp) return;

    char remote_name[256] = "";
    if (fgets(remote_name, sizeof(remote_name), fp) != NULL) {
        // Remove newline
        remote_name[strcspn(remote_name, "\n")] = 0;
    }
    pclose(fp);

    if (strlen(remote_name) == 0) {
        // No remote configured, skip this repo
        return;
    }

    // Get current branch
    snprintf(cmd, sizeof(cmd), "cd '%s' && git branch --show-current 2>/dev/null", repo->repo_path);
    fp = popen(cmd, "r");
    if (!fp) return;

    char branch_name[256] = "";
    if (fgets(branch_name, sizeof(branch_name), fp) != NULL) {
        branch_name[strcspn(branch_name, "\n")] = 0;
    }
    pclose(fp);

    if (strlen(branch_name) == 0) {
        // Not on any branch, skip
        return;
    }

    // Check for unpushed commits: git log --oneline origin/branch..HEAD
    snprintf(cmd, sizeof(cmd), "cd '%s' && git log --oneline %s/%s..HEAD 2>/dev/null",
             repo->repo_path, remote_name, branch_name);

    fp = popen(cmd, "r");
    if (!fp) return;

    // Parse each line of git log output
    size_t commit_index = 0;
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Remove newline
        buffer[strcspn(buffer, "\n")] = 0;

        // Skip empty lines
        if (strlen(buffer) == 0) continue;

        // Add commit info (first 7 chars should be hash, rest is message)
        add_unpushed_commit(repo, buffer);

        // Get files changed in this commit
        get_commit_files(repo, commit_index);
        commit_index++;
    }

    pclose(fp);
}

// Parse git-submodules report to find repositories to check
void parse_git_submodules_report(unpushed_collection_t* collection, const char* report_path) {
    printf("Reading git-submodules JSON report from: %s\n", report_path);

    json_value_t* report = json_parse_file(report_path);
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to parse git-submodules report\n");
        return;
    }

    // Get repositories array
    json_value_t* repos = get_nested_value(report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "No repositories found in report\n");
        json_free(report);
        return;
    }

    printf("Found %zu repositories in git-submodules report\n", repos->value.arr_val->count);

    // Process each repository
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo_obj = repos->value.arr_val->items[i];
        if (repo_obj->type != JSON_OBJECT) continue;

        // Get repository info
        json_value_t* name = get_nested_value(repo_obj, "name");
        json_value_t* path = get_nested_value(repo_obj, "path");

        if (name && name->type == JSON_STRING &&
            path && path->type == JSON_STRING) {

            printf("Found repo: %s at %s\n", name->value.str_val, path->value.str_val);

            // Check if it's a submodule path (should be filtered)
            if (is_submodule_path(collection, path->value.str_val)) {
                printf("  Skipping submodule: %s\n", path->value.str_val);
                continue;
            }

            // Add repository to check
            add_repo(collection, path->value.str_val, name->value.str_val);

            // Get submodule paths for filtering
            json_value_t* submodules = get_nested_value(repo_obj, "submodules");
            if (submodules && submodules->type == JSON_ARRAY) {
                for (size_t j = 0; j < submodules->value.arr_val->count; j++) {
                    json_value_t* submodule = submodules->value.arr_val->items[j];
                    if (submodule->type == JSON_STRING) {
                        add_submodule_path(collection, submodule->value.str_val);
                    }
                }
            }
        }
    }

    json_free(report);
    printf("Collected %zu submodule paths for filtering\n", collection->submodule_count);
}

// Generate JSON report
void generate_report(unpushed_collection_t* collection) {
    // Create JSON report
    json_value_t* report = json_create_object();
    json_object_set(report, "report_type", json_create_string("committed_not_pushed_analysis"));
    json_object_set(report, "generated_by", json_create_string("committed-not-pushed analyzer"));

    // Add timestamp
    time_t now = time(NULL);
    json_object_set(report, "timestamp", json_create_number((double)now));

    // Create repositories array
    json_value_t* repos_array = json_create_array();

    size_t total_unpushed_repos = 0;
    size_t total_unpushed_commits = 0;

    for (size_t i = 0; i < collection->count; i++) {
        unpushed_repo_t* repo = &collection->repos[i];

        // Only include repos with unpushed commits
        if (repo->commit_count > 0) {
            total_unpushed_repos++;
            total_unpushed_commits += repo->commit_count;

            json_value_t* repo_obj = json_create_object();
            json_object_set(repo_obj, "name", json_create_string(repo->repo_name));
            json_object_set(repo_obj, "path", json_create_string(repo->repo_path));
            json_object_set(repo_obj, "unpushed_commit_count", json_create_number((double)repo->commit_count));

            // Create commits array with file details
            json_value_t* commits_array = json_create_array();
            for (size_t j = 0; j < repo->commit_count; j++) {
                json_value_t* commit_obj = json_create_object();
                json_object_set(commit_obj, "commit_info", json_create_string(repo->unpushed_commits[j]));

                // Add files changed in this commit
                json_value_t* files_array = json_create_array();
                for (size_t k = 0; k < repo->commit_file_counts[j]; k++) {
                    char* truncated_file = truncate_filename(repo->commit_files[j][k], 1); // 1 = is_file
                    json_array_add(files_array, json_create_string(truncated_file));
                    free(truncated_file);
                }
                json_object_set(commit_obj, "files_changed", files_array);

                json_array_add(commits_array, commit_obj);
            }
            json_object_set(repo_obj, "unpushed_commits", commits_array);

            json_array_add(repos_array, repo_obj);
        }
    }

    json_object_set(report, "repositories", repos_array);

    // Create summary
    json_value_t* summary = json_create_object();
    json_object_set(summary, "total_unpushed_repositories", json_create_number((double)total_unpushed_repos));
    json_object_set(summary, "total_unpushed_commits", json_create_number((double)total_unpushed_commits));
    json_object_set(report, "summary", summary);

    // Write report to file
    json_write_file("committed-not-pushed-report.json", report);
    printf("Committed-not-pushed analysis report generated\n");

    json_free(report);
}

// Cleanup collection
void unpushed_collection_cleanup(unpushed_collection_t* collection) {
    if (collection) {
        for (size_t i = 0; i < collection->count; i++) {
            unpushed_repo_t* repo = &collection->repos[i];
            free(repo->repo_path);
            free(repo->repo_name);
            for (size_t j = 0; j < repo->commit_count; j++) {
                free(repo->unpushed_commits[j]);
                // Free files for this commit
                for (size_t k = 0; k < repo->commit_file_counts[j]; k++) {
                    free(repo->commit_files[j][k]);
                }
                free(repo->commit_files[j]);
            }
            free(repo->unpushed_commits);
            free(repo->commit_files);
            free(repo->commit_file_counts);
        }
        free(collection->repos);

        for (size_t i = 0; i < collection->submodule_count; i++) {
            free(collection->submodule_paths[i]);
        }
        free(collection->submodule_paths);

        free(collection);
    }
}

int main(int argc, char* argv[]) {
    printf("Committed Not Pushed Analyzer starting...\n");

    // Get the module path
    char module_path[1024];
    if (!getcwd(module_path, sizeof(module_path))) {
        fprintf(stderr, "Error: Cannot get current working directory\n");
        return 1;
    }

    // Load configuration
    committed_not_pushed_config_t* config = load_config(module_path);
    if (!config) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tree") == 0 || strcmp(argv[i], "-t") == 0) {
            config->current_view = VIEW_TREE;
        } else if (strcmp(argv[i], "--flat") == 0 || strcmp(argv[i], "-f") == 0) {
            config->current_view = VIEW_FLAT;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --tree, -t    Display in tree format\n");
            printf("  --flat, -f    Display in flat format (default)\n");
            printf("  --help, -h    Show this help message\n");
            free(config->repo_path);
            free(config->display_mode);
            free(config->tree_prefix);
            free(config->tree_last_prefix);
            free(config->tree_indent);
            free(config);
            return 0;
        }
    }

    // Initialize collection
    unpushed_collection_t* collection = unpushed_collection_init();
    if (!collection) {
        fprintf(stderr, "Failed to initialize collection\n");
        free(config->repo_path);
        free(config->display_mode);
        free(config->tree_prefix);
        free(config->tree_last_prefix);
        free(config->tree_indent);
        free(config);
        return 1;
    }

    // Parse git-submodules report
    parse_git_submodules_report(collection, "../git-submodules.report");

    // Analyze each repository for unpushed commits
    for (size_t i = 0; i < collection->count; i++) {
        unpushed_repo_t* repo = &collection->repos[i];
        printf("Analyzing unpushed commits in: %s\n", repo->repo_name);
        get_unpushed_commits(collection, repo);
        printf("  Found %zu unpushed commits\n", repo->commit_count);
    }

    // Generate report
    generate_report(collection);

    // Display results based on view mode
    if (config->current_view == VIEW_FLAT) {
        display_flat_view(collection, config);
    } else {
        display_tree_view(collection, config);
    }

    printf("Committed Not Pushed Analyzer completed\n");

    // Cleanup
    unpushed_collection_cleanup(collection);
    free(config->repo_path);
    free(config->display_mode);
    free(config->tree_prefix);
    free(config->tree_last_prefix);
    free(config->tree_indent);
    free(config);

    return 0;
}