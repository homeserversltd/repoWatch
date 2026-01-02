#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <time.h>
#include "../json-utils/json-utils.h"

// Structure for dirty file information
typedef struct {
    char* repo_path;
    char* repo_name;
    char** dirty_files;
    size_t file_count;
    size_t file_capacity;
} dirty_repo_t;

// Collection of dirty repositories
typedef struct {
    dirty_repo_t* repos;
    size_t count;
    size_t capacity;
    char** submodule_paths;  // List of submodule paths to filter out
    size_t submodule_count;
} dirty_collection_t;

// Initialize dirty collection
dirty_collection_t* dirty_collection_init() {
    dirty_collection_t* collection = calloc(1, sizeof(dirty_collection_t));
    if (!collection) return NULL;

    collection->capacity = 8;
    collection->repos = calloc(collection->capacity, sizeof(dirty_repo_t));
    if (!collection->repos) {
        free(collection);
        return NULL;
    }

    collection->submodule_paths = NULL;
    collection->submodule_count = 0;

    return collection;
}

// Check if a path is a submodule
int is_submodule_path(dirty_collection_t* collection, const char* path) {
    for (size_t i = 0; i < collection->submodule_count; i++) {
        if (strcmp(collection->submodule_paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

// Add submodule path to collection
void add_submodule_path(dirty_collection_t* collection, const char* path) {
    // Check if already exists
    if (is_submodule_path(collection, path)) return;

    // Resize array
    char** new_paths = realloc(collection->submodule_paths,
                              (collection->submodule_count + 1) * sizeof(char*));
    if (!new_paths) return;

    collection->submodule_paths = new_paths;
    collection->submodule_paths[collection->submodule_count] = strdup(path);
    collection->submodule_count++;
}

// Collect all submodule paths by reading .gitmodules file
void collect_submodule_paths(dirty_collection_t* collection, const char* repo_path) {
    char gitmodules_path[2048];
    snprintf(gitmodules_path, sizeof(gitmodules_path), "%s/.gitmodules", repo_path);

    FILE* fp = fopen(gitmodules_path, "r");
    if (!fp) {
        // No .gitmodules file, no submodules
        return;
    }

    char line[1024];
    int in_submodule_section = 0;

    while (fgets(line, sizeof(line), fp)) {
        // Remove trailing newline
        char* newline = strchr(line, '\n');
        if (newline) *newline = '\0';

        // Check for submodule section start
        if (strstr(line, "[submodule ")) {
            in_submodule_section = 1;
            continue;
        }

        // If we're in a submodule section, look for path
        if (in_submodule_section && strstr(line, "path = ")) {
            char* path_start = strstr(line, "path = ") + 7; // Skip "path = "
            if (path_start) {
                add_submodule_path(collection, path_start);
            }
            in_submodule_section = 0; // Reset for next submodule
        }
    }

    fclose(fp);
}

// Add dirty repository to collection
void add_dirty_repo(dirty_collection_t* collection, const char* path, const char* name) {
    if (collection->count >= collection->capacity) {
        collection->capacity *= 2;
        dirty_repo_t* new_repos = realloc(collection->repos, collection->capacity * sizeof(dirty_repo_t));
        if (!new_repos) return;
        collection->repos = new_repos;
    }

    dirty_repo_t* repo = &collection->repos[collection->count];
    repo->repo_path = strdup(path);
    repo->repo_name = strdup(name);
    repo->file_capacity = 16;
    repo->file_count = 0;
    repo->dirty_files = calloc(repo->file_capacity, sizeof(char*));

    collection->count++;
}

// Add dirty file to repository
void add_dirty_file(dirty_repo_t* repo, const char* filename) {
    if (repo->file_count >= repo->file_capacity) {
        repo->file_capacity *= 2;
        char** new_files = realloc(repo->dirty_files, repo->file_capacity * sizeof(char*));
        if (!new_files) return;
        repo->dirty_files = new_files;
    }

    repo->dirty_files[repo->file_count] = strdup(filename);
    repo->file_count++;
}

// Get git status for dirty files in a specific repository
void get_dirty_files(dirty_collection_t* collection, dirty_repo_t* repo) {
    char cmd[2048];
    FILE* fp;
    char buffer[1024];

    // Run git status --porcelain to get dirty files
    snprintf(cmd, sizeof(cmd), "cd '%s' && git status --porcelain 2>/dev/null", repo->repo_path);

    fp = popen(cmd, "r");
    if (!fp) return;

    // Parse each line of git status output
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Git status --porcelain format: XY filename
        // Where X = index status, Y = working tree status
        // We want files that are not clean (not "  " at start)

        // Skip empty lines
        if (strlen(buffer) < 4) continue;

        // Check if file has changes (not clean)
        if (buffer[0] != ' ' || buffer[1] != ' ') {
            // Extract filename (skip first 3 chars for status codes)
            char* filename = buffer + 3;

            // Remove trailing newline
            char* newline = strchr(filename, '\n');
            if (newline) *newline = '\0';

            // Add to dirty files list (submodule directories won't appear as files in git status)
            add_dirty_file(repo, filename);
        }
    }

    pclose(fp);
}

// Parse git-submodules report to find dirty repositories
// Now reads from centralized state.json
void parse_git_submodules_report(dirty_collection_t* collection, const char* report_path) {
    (void)report_path; // Unused parameter, kept for compatibility
    
    printf("Reading git-submodules data from state.json\n");

    // Load state.json and get git_submodules section
    json_value_t* state = state_load(NULL);
    if (!state) {
        fprintf(stderr, "Could not load state.json\n");
        return;
    }

    json_value_t* root = state_get_section(state, "git_submodules");
    if (!root || root->type != JSON_OBJECT) {
        fprintf(stderr, "Could not find git_submodules section in state.json or invalid format\n");
        json_free(state);
        return;
    }

    // Get repositories array
    json_value_t* repos = NULL;
    for (size_t i = 0; i < root->value.obj_val->count; i++) {
        json_entry_t* entry = root->value.obj_val->entries[i];
        if (strcmp(entry->key, "repositories") == 0 && entry->value->type == JSON_ARRAY) {
            repos = entry->value;
            break;
        }
    }

    if (!repos) {
        fprintf(stderr, "No repositories array found in git_submodules section\n");
        json_free(state);
        return;
    }

    // Parse each repository
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo_obj = repos->value.arr_val->items[i];
        if (repo_obj->type != JSON_OBJECT) continue;

        char* repo_name = NULL;
        char* repo_path = NULL;
        int is_clean = 1;

        // Extract repository data
        for (size_t j = 0; j < repo_obj->value.obj_val->count; j++) {
            json_entry_t* entry = repo_obj->value.obj_val->entries[j];

            if (strcmp(entry->key, "name") == 0 && entry->value->type == JSON_STRING) {
                repo_name = entry->value->value.str_val;
            } else if (strcmp(entry->key, "path") == 0 && entry->value->type == JSON_STRING) {
                repo_path = entry->value->value.str_val;
            } else if (strcmp(entry->key, "is_clean") == 0 && entry->value->type == JSON_BOOL) {
                is_clean = entry->value->value.bool_val;
            }
        }

        // Add all repositories to our collection (we'll check each one for dirty files)
        if (repo_name && repo_path) {
            printf("Found repo: %s at %s (%s)\n", repo_name, repo_path, is_clean ? "clean" : "dirty");
            add_dirty_repo(collection, repo_path, repo_name);
        }
    }

    json_free(state); // Free state, root is part of it
}

// Run git-submodules and parse its output to find dirty repositories
void run_git_submodules_analysis(dirty_collection_t* collection) {
    printf("Running git-submodules analysis...\n");

    // Execute git-submodules and capture its output
    FILE* fp = popen("../git-submodules/git-submodules 2>/dev/null", "r");
    if (!fp) {
        fprintf(stderr, "Could not execute git-submodules\n");
        return;
    }

    printf("Successfully opened pipe to git-submodules\n");

    char line[2048];
    int found_dirty_header = 0;
    int line_count = 0;

    printf("Reading git-submodules output:\n");
    while (fgets(line, sizeof(line), fp) != NULL) {
        line_count++;
        printf("Line %d: %s", line_count, line);

        // Look for "Dirty repositories:" header
        if (strstr(line, "Dirty repositories:")) {
            found_dirty_header = 1;
            printf("Found dirty repositories header\n");
            continue;
        }

        if (found_dirty_header) {
            // Parse lines like "  - repo_name (path)"
            if (strstr(line, "  - ")) {
                printf("Found dirty repo line: %s", line);
                char* repo_info = line + 4; // Skip "  - "
                char* paren_start = strchr(repo_info, '(');
                char* paren_end = strchr(repo_info, ')');

                if (paren_start && paren_end) {
                    // Extract repo name
                    *paren_start = '\0'; // Null terminate name
                    char repo_name[256];
                    strcpy(repo_name, repo_info);

                    // Extract repo path
                    char repo_path[2048];
                    strcpy(repo_path, paren_start + 1);
                    repo_path[paren_end - paren_start - 1] = '\0'; // Remove closing paren

                    printf("Parsed: name='%s', path='%s'\n", repo_name, repo_path);

                    // Add to collection
                    add_dirty_repo(collection, repo_path, repo_name);
                } else {
                    printf("Could not parse repo info from line\n");
                }
            }
        }
    }

    printf("Finished reading %d lines from git-submodules\n", line_count);

    pclose(fp);
}

// Generate report file
void generate_json_report(dirty_collection_t* collection) {
    // Create root JSON object
    json_value_t* root = json_create_object();
    if (!root) {
        fprintf(stderr, "Failed to create JSON root object\n");
        return;
    }

    // Add metadata
    json_object_set(root, "report_type", json_create_string("dirty_files_analysis"));
    json_object_set(root, "generated_by", json_create_string("dirty-files analyzer"));
    json_object_set(root, "timestamp", json_create_number((double)time(NULL)));

    // Create repositories array
    json_value_t* repos_array = json_create_array();
    if (!repos_array) {
        json_free(root);
        return;
    }

    size_t total_dirty_files = 0;

    for (size_t i = 0; i < collection->count; i++) {
        dirty_repo_t* repo = &collection->repos[i];

        // Create repository object
        json_value_t* repo_obj = json_create_object();
        if (!repo_obj) continue;

        json_object_set(repo_obj, "name", json_create_string(repo->repo_name));
        json_object_set(repo_obj, "path", json_create_string(repo->repo_path));
        json_object_set(repo_obj, "dirty_file_count", json_create_number((double)repo->file_count));

        // Create files array
        json_value_t* files_array = json_create_array();
        if (files_array) {
            for (size_t j = 0; j < repo->file_count; j++) {
                json_array_add(files_array, json_create_string(repo->dirty_files[j]));
            }
            json_object_set(repo_obj, "dirty_files", files_array);
        }

        json_array_add(repos_array, repo_obj);
        total_dirty_files += repo->file_count;
    }

    // Add repositories array to root
    json_object_set(root, "repositories", repos_array);

    // Create summary object
    json_value_t* summary = json_create_object();
    if (summary) {
        json_object_set(summary, "total_dirty_repositories", json_create_number((double)collection->count));
        json_object_set(summary, "total_dirty_files", json_create_number((double)total_dirty_files));
        json_object_set(root, "summary", summary);
    }

    // Write to centralized state.json
    if (state_update_section(NULL, "dirty_files", root) != 0) {
        fprintf(stderr, "Failed to update state.json dirty_files section\n");
    }
    // Note: state_update_section takes ownership of root, don't free it here
}

// Cleanup dirty collection
void dirty_collection_cleanup(dirty_collection_t* collection) {
    if (collection) {
        for (size_t i = 0; i < collection->count; i++) {
            dirty_repo_t* repo = &collection->repos[i];
            free(repo->repo_path);
            free(repo->repo_name);

            for (size_t j = 0; j < repo->file_count; j++) {
                free(repo->dirty_files[j]);
            }
            free(repo->dirty_files);
        }
        free(collection->repos);

        // Cleanup submodule paths
        for (size_t i = 0; i < collection->submodule_count; i++) {
            free(collection->submodule_paths[i]);
        }
        free(collection->submodule_paths);

        free(collection);
    }
}

int main(int argc, char* argv[]) {
    printf("Dirty Files Analyzer starting...\n");

    // Initialize collection
    dirty_collection_t* collection = dirty_collection_init();
    if (!collection) {
        fprintf(stderr, "Failed to initialize dirty collection\n");
        return 1;
    }

    // Parse git-submodules data from centralized state.json
    parse_git_submodules_report(collection, NULL);

    // Collect all submodule paths for filtering
    collect_submodule_paths(collection, ".");

    printf("Found %zu dirty repositories from git-submodules report\n", collection->count);
    printf("Collected %zu submodule paths for filtering\n", collection->submodule_count);

    // For each repository, get the specific dirty files
    for (size_t i = 0; i < collection->count; i++) {
        dirty_repo_t* repo = &collection->repos[i];
        printf("Analyzing dirty files in: %s\n", repo->repo_name);
        get_dirty_files(collection, repo);
        printf("  Found %zu dirty files\n", repo->file_count);
    }

    // Filter out repositories with no dirty files
    size_t write_idx = 0;
    for (size_t i = 0; i < collection->count; i++) {
        if (collection->repos[i].file_count > 0) {
            if (write_idx != i) {
                collection->repos[write_idx] = collection->repos[i];
            }
            write_idx++;
        } else {
            // Free the repository that has no dirty files
            free(collection->repos[i].repo_path);
            free(collection->repos[i].repo_name);
            for (size_t j = 0; j < collection->repos[i].file_count; j++) {
                free(collection->repos[i].dirty_files[j]);
            }
            free(collection->repos[i].dirty_files);
        }
    }
    collection->count = write_idx;

    // Generate JSON report
    generate_json_report(collection);
    printf("Dirty files analysis report generated\n");

    // Print summary to stdout
    size_t total_files = 0;
    for (size_t i = 0; i < collection->count; i++) {
        total_files += collection->repos[i].file_count;
    }

    printf("\nDirty Files Analysis Summary:\n");
    printf("  Total dirty repositories: %zu\n", collection->count);
    printf("  Total dirty files: %zu\n", total_files);

    if (collection->count > 0) {
        printf("\nDetailed breakdown:\n");
        for (size_t i = 0; i < collection->count; i++) {
            dirty_repo_t* repo = &collection->repos[i];
            printf("  %s (%s): %zu dirty files\n", repo->repo_name, repo->repo_path, repo->file_count);

            // Show first few dirty files as examples
            if (repo->file_count > 0) {
                size_t show_count = repo->file_count > 3 ? 3 : repo->file_count;
                for (size_t j = 0; j < show_count; j++) {
                    printf("    - %s\n", repo->dirty_files[j]);
                }
                if (repo->file_count > 3) {
                    printf("    ... and %zu more\n", repo->file_count - 3);
                }
            }
        }
    }

    // Cleanup
    dirty_collection_cleanup(collection);

    printf("Dirty Files Analyzer completed\n");
    return 0;
}
