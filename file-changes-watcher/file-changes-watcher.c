#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <regex.h>
#include "../json-utils/json-utils.h"

// Structure for file change tracking
typedef struct {
    char* path;
    char* repository;
    time_t first_detected;
    time_t last_updated;
    time_t mtime;  // File modification time
} file_change_t;

// Collection of file changes
typedef struct {
    file_change_t* files;
    size_t count;
    size_t capacity;
} file_changes_t;

// Initialize file changes collection
file_changes_t* file_changes_init() {
    file_changes_t* changes = calloc(1, sizeof(file_changes_t));
    if (!changes) return NULL;

    changes->capacity = 100;
    changes->files = calloc(changes->capacity, sizeof(file_change_t));
    if (!changes->files) {
        free(changes);
        return NULL;
    }

    return changes;
}

// Cleanup file changes collection
void file_changes_cleanup(file_changes_t* changes) {
    if (!changes) return;

    for (size_t i = 0; i < changes->count; i++) {
        free(changes->files[i].path);
        free(changes->files[i].repository);
    }
    free(changes->files);
    free(changes);
}

// Add or update file change entry
void add_file_change(file_changes_t* changes, const char* path, const char* repository) {
    if (!changes || !path || !repository) return;

    time_t now = time(NULL);
    char full_path[2048];

    // Create full path: repository/path
    snprintf(full_path, sizeof(full_path), "%s/%s", repository, path);

    // Check if file already exists
    for (size_t i = 0; i < changes->count; i++) {
        if (strcmp(changes->files[i].path, full_path) == 0) {
            // Update last_updated timestamp (resets the timer)
            changes->files[i].last_updated = now;
            return;
        }
    }

    // File doesn't exist, add it
    if (changes->count >= changes->capacity) {
        changes->capacity *= 2;
        file_change_t* new_files = realloc(changes->files, changes->capacity * sizeof(file_change_t));
        if (!new_files) return;
        changes->files = new_files;
    }

    file_change_t* change = &changes->files[changes->count];
    change->path = strdup(full_path);
    change->repository = strdup(repository);
    change->first_detected = now;
    change->last_updated = now;
    changes->count++;
}

// Remove file changes that are no longer in dirty-files-report.json
void remove_stale_changes(file_changes_t* changes, json_value_t* dirty_report) {
    if (!changes || !dirty_report) return;

    // Create a set of all current dirty file paths
    char** current_paths = NULL;
    size_t current_count = 0;

    json_value_t* repos = get_nested_value(dirty_report, "repositories");
    if (repos && repos->type == JSON_ARRAY) {
        for (size_t i = 0; i < repos->value.arr_val->count; i++) {
            json_value_t* repo = repos->value.arr_val->items[i];
            if (repo->type != JSON_OBJECT) continue;

            json_value_t* repo_name = get_nested_value(repo, "name");
            json_value_t* files = get_nested_value(repo, "dirty_files");

            if (!repo_name || repo_name->type != JSON_STRING) continue;
            if (!files || files->type != JSON_ARRAY) continue;

            for (size_t j = 0; j < files->value.arr_val->count; j++) {
                json_value_t* file = files->value.arr_val->items[j];
                if (file->type != JSON_STRING) continue;

                char full_path[2048];
                snprintf(full_path, sizeof(full_path), "%s/%s",
                        repo_name->value.str_val, file->value.str_val);

                current_paths = realloc(current_paths, (current_count + 1) * sizeof(char*));
                current_paths[current_count] = strdup(full_path);
                current_count++;
            }
        }
    }

    // Remove changes that are no longer in the dirty report
    size_t write_idx = 0;
    for (size_t i = 0; i < changes->count; i++) {
        file_change_t* change = &changes->files[i];
        int still_exists = 0;

        for (size_t j = 0; j < current_count; j++) {
            if (strcmp(change->path, current_paths[j]) == 0) {
                still_exists = 1;
                break;
            }
        }

        if (still_exists) {
            // Keep this change
            if (write_idx != i) {
                changes->files[write_idx] = changes->files[i];
            }
            write_idx++;
        } else {
            // Remove this change
            free(change->path);
            free(change->repository);
        }
    }
    changes->count = write_idx;

    // Cleanup current paths
    for (size_t i = 0; i < current_count; i++) {
        free(current_paths[i]);
    }
    free(current_paths);
}

// Get tracked files from a repository using git ls-files
char** get_tracked_files_from_repo(const char* repo_path, size_t* file_count) {
    *file_count = 0;

    // Change to repository directory
    char original_cwd[1024];
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        fprintf(stderr, "Failed to get current working directory\n");
        return NULL;
    }

    if (chdir(repo_path) != 0) {
        fprintf(stderr, "Failed to change to repository directory: %s\n", repo_path);
        return NULL;
    }

    // Run git ls-files command
    FILE* fp = popen("git ls-files 2>/dev/null", "r");
    if (!fp) {
        fprintf(stderr, "Failed to run git ls-files in %s\n", repo_path);
        chdir(original_cwd);
        return NULL;
    }

    // Read output and count files
    char line[2048];
    size_t capacity = 100;
    char** files = calloc(capacity, sizeof(char*));
    if (!files) {
        pclose(fp);
        chdir(original_cwd);
        return NULL;
    }

    while (fgets(line, sizeof(line), fp)) {
        // Remove trailing newline
        char* newline = strchr(line, '\n');
        if (newline) *newline = '\0';

        // Skip empty lines
        if (strlen(line) == 0) continue;

        // Resize array if needed
        if (*file_count >= capacity) {
            capacity *= 2;
            char** new_files = realloc(files, capacity * sizeof(char*));
            if (!new_files) {
                // Cleanup on error
                for (size_t i = 0; i < *file_count; i++) {
                    free(files[i]);
                }
                free(files);
                pclose(fp);
                chdir(original_cwd);
                return NULL;
            }
            files = new_files;
        }

        files[*file_count] = strdup(line);
        (*file_count)++;
    }

    pclose(fp);

    // Change back to original directory
    chdir(original_cwd);

    return files;
}

// Get file modification time using stat()
time_t get_file_mtime(const char* filepath) {
    struct stat file_stat;

    if (stat(filepath, &file_stat) != 0) {
        // File doesn't exist or can't be accessed
        return 0;
    }

    return file_stat.st_mtime;
}

// Load existing file changes from file-changes-report.json
int load_existing_changes(file_changes_t* changes) {
    json_value_t* report = json_parse_file("file-changes-report.json");
    if (!report || report->type != JSON_OBJECT) {
        return -1; // No existing report or invalid format
    }

    json_value_t* files = get_nested_value(report, "files");
    if (!files || files->type != JSON_ARRAY) {
        json_free(report);
        return -1;
    }

    for (size_t i = 0; i < files->value.arr_val->count; i++) {
        json_value_t* file_obj = files->value.arr_val->items[i];
        if (file_obj->type != JSON_OBJECT) continue;

        json_value_t* path = get_nested_value(file_obj, "path");
        json_value_t* repository = get_nested_value(file_obj, "repository");
        json_value_t* first_detected = get_nested_value(file_obj, "first_detected");
        json_value_t* last_updated = get_nested_value(file_obj, "last_updated");
        json_value_t* mtime = get_nested_value(file_obj, "mtime");

        if (path && path->type == JSON_STRING &&
            repository && repository->type == JSON_STRING &&
            first_detected && first_detected->type == JSON_NUMBER &&
            last_updated && last_updated->type == JSON_NUMBER) {

            if (changes->count >= changes->capacity) {
                changes->capacity *= 2;
                file_change_t* new_files = realloc(changes->files, changes->capacity * sizeof(file_change_t));
                if (!new_files) continue;
                changes->files = new_files;
            }

            file_change_t* change = &changes->files[changes->count];
            change->path = strdup(path->value.str_val);
            change->repository = strdup(repository->value.str_val);
            change->first_detected = (time_t)first_detected->value.num_val;
            change->last_updated = (time_t)last_updated->value.num_val;
            // Handle mtime field (may not exist in old reports)
            change->mtime = mtime && mtime->type == JSON_NUMBER ? (time_t)mtime->value.num_val : 0;
            changes->count++;
        }
    }

    json_free(report);
    return 0;
}

// Save file changes to file-changes-report.json
void save_file_changes(file_changes_t* changes) {
    json_value_t* root = json_create_object();
    if (!root) return;

    json_object_set(root, "report_type", json_create_string("file_changes_tracking"));
    json_object_set(root, "generated_by", json_create_string("file-changes-watcher"));
    json_object_set(root, "timestamp", json_create_number((double)time(NULL)));

    json_value_t* files_array = json_create_array();
    if (files_array) {
        for (size_t i = 0; i < changes->count; i++) {
            file_change_t* change = &changes->files[i];

            json_value_t* file_obj = json_create_object();
            if (file_obj) {
                json_object_set(file_obj, "path", json_create_string(change->path));
                json_object_set(file_obj, "repository", json_create_string(change->repository));
                json_object_set(file_obj, "first_detected", json_create_number((double)change->first_detected));
                json_object_set(file_obj, "last_updated", json_create_number((double)change->last_updated));
                json_object_set(file_obj, "mtime", json_create_number((double)change->mtime));
                json_array_add(files_array, file_obj);
            }
        }
        json_object_set(root, "files", files_array);
    }

    json_write_file("file-changes-report.json", root);
    json_free(root);
}

// Poll all tracked files for mtime changes
void poll_tracked_files_mtime(file_changes_t* changes) {
    // Load existing changes if this is the first run
    static int first_run = 1;
    if (first_run) {
        load_existing_changes(changes);
        first_run = 0;
    }

    // Read git-submodules.report to get all repositories
    json_value_t* submodules_report = json_parse_file("git-submodules.report");
    if (!submodules_report || submodules_report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load git-submodules.report\n");
        return;
    }

    json_value_t* repos = get_nested_value(submodules_report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "No repositories found in git-submodules.report\n");
        json_free(submodules_report);
        return;
    }

    time_t now = time(NULL);

    // Create a set of all currently tracked files to detect removed files
    char** current_tracked_files = NULL;
    size_t current_tracked_count = 0;

    // Process each repository
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo = repos->value.arr_val->items[i];
        if (repo->type != JSON_OBJECT) continue;

        json_value_t* repo_name = get_nested_value(repo, "name");
        json_value_t* repo_path = get_nested_value(repo, "path");

        if (!repo_name || repo_name->type != JSON_STRING) continue;
        if (!repo_path || repo_path->type != JSON_STRING) continue;

        const char* repo_name_str = repo_name->value.str_val;
        const char* repo_path_str = repo_path->value.str_val;

        // Get all tracked files from this repository
        size_t file_count = 0;
        char** tracked_files = get_tracked_files_from_repo(repo_path_str, &file_count);

        if (!tracked_files) {
            fprintf(stderr, "Failed to get tracked files from repository: %s\n", repo_name_str);
            continue;
        }

        // Process each tracked file
        for (size_t j = 0; j < file_count; j++) {
            const char* file_path = tracked_files[j];

            // Build full path for mtime checking (relative to repoWatch root)
            char full_path[4096];
            if (strcmp(repo_name_str, "root") == 0) {
                // Root repo: file path is relative to repoWatch directory
                snprintf(full_path, sizeof(full_path), "../%s", file_path);
            } else {
                // Submodule: file path is relative to submodule directory
                snprintf(full_path, sizeof(full_path), "../%s/%s", repo_name_str, file_path);
            }

            // Get current mtime
            time_t current_mtime = get_file_mtime(full_path);

            // Build path key for tracking (repo/file format)
            char path_key[4096];
            if (strcmp(repo_name_str, "root") == 0) {
                // For root repo, use just the filename (to match existing format)
                snprintf(path_key, sizeof(path_key), "%s", file_path);
            } else {
                // For submodules, use repo/filename format
                snprintf(path_key, sizeof(path_key), "%s/%s", repo_name_str, file_path);
            }

            // Check if we already track this file
            int found = 0;
            for (size_t k = 0; k < changes->count; k++) {
                file_change_t* change = &changes->files[k];
                if (strcmp(change->path, path_key) == 0) {
                    found = 1;
                    // File exists - check if mtime changed
                    if (change->mtime != current_mtime && current_mtime != 0) {
                        // File was modified - update timestamp and mtime
                        change->last_updated = now;
                        change->mtime = current_mtime;
                    }
                    break;
                }
            }

            // If not found, add new file
            if (!found && current_mtime != 0) {
                if (changes->count >= changes->capacity) {
                    changes->capacity *= 2;
                    file_change_t* new_files = realloc(changes->files, changes->capacity * sizeof(file_change_t));
                    if (!new_files) {
                        fprintf(stderr, "Failed to allocate memory for new file changes\n");
                        break;
                    }
                    changes->files = new_files;
                }

                file_change_t* change = &changes->files[changes->count];
                change->path = strdup(path_key);
                change->repository = strcmp(repo_name_str, "root") == 0 ? strdup("root") : strdup(repo_name_str);
                change->first_detected = now;
                change->last_updated = now;
                change->mtime = current_mtime;
                changes->count++;
            }

            // Add to current tracked files list for cleanup
            current_tracked_files = realloc(current_tracked_files, (current_tracked_count + 1) * sizeof(char*));
            current_tracked_files[current_tracked_count] = strdup(path_key);
            current_tracked_count++;
        }

        // Cleanup tracked files array
        for (size_t j = 0; j < file_count; j++) {
            free(tracked_files[j]);
        }
        free(tracked_files);
    }

    // Remove files that are no longer tracked by git
    size_t write_idx = 0;
    for (size_t i = 0; i < changes->count; i++) {
        file_change_t* change = &changes->files[i];
        int still_tracked = 0;

        for (size_t j = 0; j < current_tracked_count; j++) {
            if (strcmp(change->path, current_tracked_files[j]) == 0) {
                still_tracked = 1;
                break;
            }
        }

        if (still_tracked) {
            // Keep this change
            if (write_idx != i) {
                changes->files[write_idx] = changes->files[i];
            }
            write_idx++;
        } else {
            // Remove this change (file no longer tracked)
            free(change->path);
            free(change->repository);
        }
    }
    changes->count = write_idx;

    // Cleanup current tracked files
    for (size_t i = 0; i < current_tracked_count; i++) {
        free(current_tracked_files[i]);
    }
    free(current_tracked_files);

    json_free(submodules_report);
}

int main(int argc, char* argv[]) {
    file_changes_t* changes = file_changes_init();
    if (!changes) {
        fprintf(stderr, "Failed to initialize file changes collection\n");
        return 1;
    }

    // Poll all tracked files for mtime changes
    poll_tracked_files_mtime(changes);

    // Save the updated file changes report
    save_file_changes(changes);

    file_changes_cleanup(changes);
    return 0;
}

