#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include "../json-utils/json-utils.h"

// Structure for file change tracking
typedef struct {
    char* path;
    char* repository;
    time_t first_detected;
    time_t last_updated;
} file_change_t;

// Collection of file changes
typedef struct {
    file_change_t* files;
    size_t count;
    size_t capacity;
} file_changes_t;

// Repository tracking structure
typedef struct {
    char* path;           // Full path to repository
    char* name;           // Repository name
    char** tracked_files; // Array of tracked file paths (relative to repo root)
    size_t tracked_count;
    size_t tracked_capacity;
    int watch_fd;         // Inotify watch descriptor
} repository_t;

// Collection of repositories
typedef struct {
    repository_t* repos;
    size_t count;
    size_t capacity;
} repository_collection_t;

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

// Initialize repository collection
repository_collection_t* repository_collection_init() {
    repository_collection_t* collection = calloc(1, sizeof(repository_collection_t));
    if (!collection) return NULL;

    collection->capacity = 16;
    collection->repos = calloc(collection->capacity, sizeof(repository_t));
    if (!collection->repos) {
        free(collection);
        return NULL;
    }

    return collection;
}

// Cleanup repository collection
void repository_collection_cleanup(repository_collection_t* collection) {
    if (!collection) return;

    for (size_t i = 0; i < collection->count; i++) {
        repository_t* repo = &collection->repos[i];
        free(repo->path);
        free(repo->name);
        for (size_t j = 0; j < repo->tracked_count; j++) {
            free(repo->tracked_files[j]);
        }
        free(repo->tracked_files);
    }
    free(collection->repos);
    free(collection);
}

// Add repository to collection
void add_repository(repository_collection_t* collection, const char* path, const char* name) {
    if (!collection || !path || !name) return;

    if (collection->count >= collection->capacity) {
        collection->capacity *= 2;
        repository_t* new_repos = realloc(collection->repos, collection->capacity * sizeof(repository_t));
        if (!new_repos) return;
        collection->repos = new_repos;
    }

    repository_t* repo = &collection->repos[collection->count];
    memset(repo, 0, sizeof(repository_t));
    repo->path = strdup(path);
    repo->name = strdup(name);
    repo->tracked_capacity = 100;
    repo->tracked_files = calloc(repo->tracked_capacity, sizeof(char*));
    repo->watch_fd = -1;

    if (repo->path && repo->name && repo->tracked_files) {
        collection->count++;
    }
}

// Add tracked file to repository
void add_tracked_file(repository_t* repo, const char* filepath) {
    if (!repo || !filepath) return;

    if (repo->tracked_count >= repo->tracked_capacity) {
        repo->tracked_capacity *= 2;
        char** new_files = realloc(repo->tracked_files, repo->tracked_capacity * sizeof(char*));
        if (!new_files) return;
        repo->tracked_files = new_files;
    }

    repo->tracked_files[repo->tracked_count] = strdup(filepath);
    repo->tracked_count++;
}

// Check if file is tracked in repository
int is_file_tracked(repository_t* repo, const char* filepath) {
    if (!repo || !filepath) return 0;

    for (size_t i = 0; i < repo->tracked_count; i++) {
        if (strcmp(repo->tracked_files[i], filepath) == 0) {
            return 1;
        }
    }
    return 0;
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
                json_array_add(files_array, file_obj);
            }
        }
        json_object_set(root, "files", files_array);
    }

    json_write_file("file-changes-report.json", root);
    json_free(root);
}

// Process dirty-files-report.json and update file changes
void process_dirty_files_report(file_changes_t* changes) {
    json_value_t* report = json_parse_file("dirty-files-report.json");
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load dirty-files-report.json\n");
        return;
    }

    // Load existing changes if this is the first run
    static int first_run = 1;
    if (first_run) {
        load_existing_changes(changes);
        first_run = 0;
    }

    // Add new files from the dirty report
    json_value_t* repos = get_nested_value(report, "repositories");
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
                if (file->type == JSON_STRING) {
                    add_file_change(changes, file->value.str_val, repo_name->value.str_val);
                }
            }
        }
    }

    // Remove files that are no longer dirty
    remove_stale_changes(changes, report);

    json_free(report);
}

int main(int argc, char* argv[]) {
    printf("File Changes Watcher starting...\n");

    file_changes_t* changes = file_changes_init();
    if (!changes) {
        fprintf(stderr, "Failed to initialize file changes collection\n");
        return 1;
    }

    // Process the current dirty files report
    process_dirty_files_report(changes);

    // Save the updated file changes report
    save_file_changes(changes);

    printf("Processed %zu file changes\n", changes->count);

    // Print summary
    printf("\nFile Changes Tracking Summary:\n");
    printf("  Total tracked files: %zu\n", changes->count);

    if (changes->count > 0) {
        printf("\nActive files:\n");
        for (size_t i = 0; i < changes->count; i++) {
            file_change_t* change = &changes->files[i];
            printf("  %s (repo: %s, updated: %ld seconds ago)\n",
                   change->path, change->repository,
                   time(NULL) - change->last_updated);
        }
    }

    file_changes_cleanup(changes);
    printf("File Changes Watcher completed\n");
    return 0;
}

