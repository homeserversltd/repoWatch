#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <regex.h>
#include "../json-utils/json-utils.h"

// Structure for mapping watch descriptors to directory information
typedef struct {
    int wd;                    // Watch descriptor
    char* dir_path;            // Full path to directory
    char* repository;          // Repository name
} watch_entry_t;

// Collection of watch entries
typedef struct {
    watch_entry_t* entries;
    size_t count;
    size_t capacity;
    int inotify_fd;            // Inotify file descriptor
} watch_collection_t;

// Global variables for daemon cleanup
static watch_collection_t* g_watches = NULL;
static volatile sig_atomic_t g_running = 1;

// Signal handler for clean shutdown
void daemon_signal_handler(int sig) {
    g_running = 0;
}

// Initialize watch collection
watch_collection_t* watch_collection_init() {
    watch_collection_t* watches = calloc(1, sizeof(watch_collection_t));
    if (!watches) return NULL;

    watches->capacity = 100;
    watches->entries = calloc(watches->capacity, sizeof(watch_entry_t));
    if (!watches->entries) {
        free(watches);
        return NULL;
    }

    // Initialize inotify
    watches->inotify_fd = inotify_init1(IN_NONBLOCK);
    if (watches->inotify_fd < 0) {
        fprintf(stderr, "Failed to initialize inotify: %s\n", strerror(errno));
        free(watches->entries);
        free(watches);
        return NULL;
    }

    return watches;
}

// Cleanup watch collection
void watch_collection_cleanup(watch_collection_t* watches) {
    if (!watches) return;

    // Remove all watches and free entries
    for (size_t i = 0; i < watches->count; i++) {
        watch_entry_t* entry = &watches->entries[i];
        if (entry->wd >= 0) {
            inotify_rm_watch(watches->inotify_fd, entry->wd);
        }
        free(entry->dir_path);
        free(entry->repository);
    }

    free(watches->entries);
    close(watches->inotify_fd);
    free(watches);
}

// Add watch for a directory
int add_directory_watch(watch_collection_t* watches, const char* dir_path, const char* repository) {
    if (!watches || !dir_path || !repository) return -1;

    // Check if directory already exists
    for (size_t i = 0; i < watches->count; i++) {
        if (strcmp(watches->entries[i].dir_path, dir_path) == 0) {
            // Already watching this directory
            return 0;
        }
    }

    // Expand capacity if needed
    if (watches->count >= watches->capacity) {
        watches->capacity *= 2;
        watch_entry_t* new_entries = realloc(watches->entries, watches->capacity * sizeof(watch_entry_t));
        if (!new_entries) return -1;
        watches->entries = new_entries;
    }

    // Add watch for directory - watch for file creation, deletion, and modification
    // Use IN_RECURSIVE if available (Linux 5.9+) to watch subdirectories
    uint32_t mask = IN_MODIFY | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
    #ifdef IN_RECURSIVE
    mask |= IN_RECURSIVE;
    #endif
    int wd = inotify_add_watch(watches->inotify_fd, dir_path, mask);
    if (wd < 0) {
        // Skip directories we can't watch (permission issues, etc.)
        fprintf(stderr, "Failed to watch directory %s: %s\n", dir_path, strerror(errno));
        return 0;
    }

    watch_entry_t* entry = &watches->entries[watches->count];
    entry->wd = wd;
    entry->dir_path = strdup(dir_path);
    entry->repository = strdup(repository);

    if (!entry->dir_path || !entry->repository) {
        // Cleanup on allocation failure
        free(entry->dir_path);
        free(entry->repository);
        inotify_rm_watch(watches->inotify_fd, wd);
        return -1;
    }

    watches->count++;
    return 0;
}

// Write a change notification to the stream file
void write_change_notification(const char* stream_file, const char* file_path, const char* repository, time_t timestamp) {
    FILE* fp = fopen(stream_file, "a");
    if (!fp) {
        // Silent failure in daemon mode - don't spam logs
        return;
    }

    // Write JSON line: {"path":"file.c","repository":"root","timestamp":1234567890}
    fprintf(fp, "{\"path\":\"%s\",\"repository\":\"%s\",\"timestamp\":%ld}\n",
            file_path, repository, (long)timestamp);

    fclose(fp);
}

// Clean up expired entries from report file (older than 30 seconds)
void cleanup_expired_report_entries(const char* report_file) {
    json_value_t* report = json_parse_file(report_file);
    if (!report || report->type != JSON_OBJECT) {
        return; // No report to clean
    }

    json_value_t* files_array = get_nested_value(report, "files");
    if (!files_array || files_array->type != JSON_ARRAY) {
        json_free(report);
        return;
    }

    time_t now = time(NULL);
    json_value_t* filtered_array = json_create_array();
    if (filtered_array) {
        for (size_t i = 0; i < files_array->value.arr_val->count; i++) {
            json_value_t* file_obj = files_array->value.arr_val->items[i];
            if (file_obj->type == JSON_OBJECT) {
                json_value_t* path_val = get_nested_value(file_obj, "path");
                json_value_t* repo_val = get_nested_value(file_obj, "repository");
                json_value_t* first_detected_val = get_nested_value(file_obj, "first_detected");
                json_value_t* last_updated_val = get_nested_value(file_obj, "last_updated");
                
                if (last_updated_val && last_updated_val->type == JSON_NUMBER) {
                    time_t last_updated = (time_t)last_updated_val->value.num_val;
                    // Keep entries updated within the last 30 seconds
                    if (now - last_updated < 30) {
                        // Create new object (don't reuse to avoid double-free)
                        json_value_t* new_file_obj = json_create_object();
                        if (path_val && path_val->type == JSON_STRING) {
                            json_object_set(new_file_obj, "path", json_create_string(path_val->value.str_val));
                        }
                        if (repo_val && repo_val->type == JSON_STRING) {
                            json_object_set(new_file_obj, "repository", json_create_string(repo_val->value.str_val));
                        }
                        if (first_detected_val && first_detected_val->type == JSON_NUMBER) {
                            json_object_set(new_file_obj, "first_detected", json_create_number(first_detected_val->value.num_val));
                        }
                        json_object_set(new_file_obj, "last_updated", json_create_number((double)last_updated));
                        json_array_add(filtered_array, new_file_obj);
                    }
                }
            }
        }
        // Only write if the filtered array is different (has different count)
        json_value_t* old_files_array = get_nested_value(report, "files");
        size_t old_count = (old_files_array && old_files_array->type == JSON_ARRAY) ? old_files_array->value.arr_val->count : 0;
        size_t new_count = filtered_array->value.arr_val->count;
        
        if (old_count != new_count) {
            // Count changed, need to update
            json_object_set(report, "files", filtered_array);
            json_object_set(report, "timestamp", json_create_number((double)now));
            json_write_file(report_file, report);
        } else {
            // No change, don't write (avoid unnecessary file updates)
            json_free(filtered_array);
        }
    }

    json_free(report);
}

// Update the file-changes-report.json with a file change
void update_file_changes_report(const char* report_file, const char* file_path, const char* repository, time_t timestamp) {
    // Build the full path for the report (repository/path/to/file)
    char report_path[4096];
    if (strcmp(repository, "root") == 0) {
        snprintf(report_path, sizeof(report_path), "root/%s", file_path);
    } else {
        snprintf(report_path, sizeof(report_path), "%s/%s", repository, file_path);
    }

    // Read existing report
    json_value_t* report = json_parse_file(report_file);
    if (!report || report->type != JSON_OBJECT) {
        // Create new report if it doesn't exist
        report = json_create_object();
        if (!report) return;
        json_object_set(report, "report_type", json_create_string("file_changes_tracking"));
        json_object_set(report, "generated_by", json_create_string("file-changes-watcher"));
        json_object_set(report, "timestamp", json_create_number((double)timestamp));
        json_object_set(report, "files", json_create_array());
    }

    // Update timestamp
    json_object_set(report, "timestamp", json_create_number((double)timestamp));

    // Get files array
    json_value_t* files_array = get_nested_value(report, "files");
    if (!files_array || files_array->type != JSON_ARRAY) {
        files_array = json_create_array();
        json_object_set(report, "files", files_array);
    }

    // Check if file already exists in report
    int found = 0;
    for (size_t i = 0; i < files_array->value.arr_val->count; i++) {
        json_value_t* file_obj = files_array->value.arr_val->items[i];
        if (file_obj->type == JSON_OBJECT) {
            json_value_t* path_val = get_nested_value(file_obj, "path");
            json_value_t* repo_val = get_nested_value(file_obj, "repository");
            if (path_val && path_val->type == JSON_STRING &&
                repo_val && repo_val->type == JSON_STRING &&
                strcmp(path_val->value.str_val, report_path) == 0 &&
                strcmp(repo_val->value.str_val, repository) == 0) {
                // Update existing entry
                json_object_set(file_obj, "last_updated", json_create_number((double)timestamp));
                found = 1;
                break;
            }
        }
    }

    // Add new entry if not found
    if (!found) {
        json_value_t* file_obj = json_create_object();
        json_object_set(file_obj, "path", json_create_string(report_path));
        json_object_set(file_obj, "repository", json_create_string(repository));
        json_object_set(file_obj, "first_detected", json_create_number((double)timestamp));
        json_object_set(file_obj, "last_updated", json_create_number((double)timestamp));
        json_array_add(files_array, file_obj);
    }

    // Clean up expired entries (older than 30 seconds) by rebuilding array with new objects
    time_t now = timestamp;
    json_value_t* filtered_array = json_create_array();
    if (filtered_array) {
        for (size_t i = 0; i < files_array->value.arr_val->count; i++) {
            json_value_t* file_obj = files_array->value.arr_val->items[i];
            if (file_obj->type == JSON_OBJECT) {
                json_value_t* path_val = get_nested_value(file_obj, "path");
                json_value_t* repo_val = get_nested_value(file_obj, "repository");
                json_value_t* first_detected_val = get_nested_value(file_obj, "first_detected");
                json_value_t* last_updated_val = get_nested_value(file_obj, "last_updated");
                
                if (last_updated_val && last_updated_val->type == JSON_NUMBER) {
                    time_t last_updated = (time_t)last_updated_val->value.num_val;
                    // Keep entries updated within the last 30 seconds
                    if (now - last_updated < 30) {
                        // Create new object (don't reuse to avoid double-free)
                        json_value_t* new_file_obj = json_create_object();
                        if (path_val && path_val->type == JSON_STRING) {
                            json_object_set(new_file_obj, "path", json_create_string(path_val->value.str_val));
                        }
                        if (repo_val && repo_val->type == JSON_STRING) {
                            json_object_set(new_file_obj, "repository", json_create_string(repo_val->value.str_val));
                        }
                        if (first_detected_val && first_detected_val->type == JSON_NUMBER) {
                            json_object_set(new_file_obj, "first_detected", json_create_number(first_detected_val->value.num_val));
                        }
                        json_object_set(new_file_obj, "last_updated", json_create_number((double)last_updated));
                        json_array_add(filtered_array, new_file_obj);
                    }
                }
            }
        }
        // Only write if the filtered array is different (has different count)
        size_t old_count = files_array->value.arr_val->count;
        size_t new_count = filtered_array->value.arr_val->count;
        
        if (old_count != new_count) {
            // Count changed, need to update
            json_object_set(report, "files", filtered_array);
            json_object_set(report, "timestamp", json_create_number((double)now));
            json_write_file(report_file, report);
        } else {
            // No change, don't write (avoid unnecessary file updates)
            json_free(filtered_array);
        }
    } else {
        // No filtered array created, just write as-is
        json_write_file(report_file, report);
    }

    json_free(report);
}

// Process inotify events and write notifications for directory changes
void process_inotify_events(watch_collection_t* watches, const char* stream_file, const char* report_file) {
    if (!watches || watches->inotify_fd < 0) return;

    char buffer[4096];
    ssize_t len;

    // Extract filenames from paths for exclusion
    const char* report_filename = strrchr(report_file, '/');
    if (!report_filename) report_filename = report_file;
    else report_filename++; // Skip the '/'
    
    const char* stream_filename = strrchr(stream_file, '/');
    if (!stream_filename) stream_filename = stream_file;
    else stream_filename++; // Skip the '/'

    // Read all available events
    while ((len = read(watches->inotify_fd, buffer, sizeof(buffer))) > 0) {
        const struct inotify_event* event;
        time_t now = time(NULL);

        for (char* ptr = buffer; ptr < buffer + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event*)ptr;

            // Find the watch entry for this event
            for (size_t i = 0; i < watches->count; i++) {
                watch_entry_t* entry = &watches->entries[i];
                if (entry->wd == event->wd && event->name) {
                    // Skip ALL report files and stream file to avoid infinite update loops
                    const char* name = event->name;
                    if (strcmp(name, stream_filename) == 0 ||
                        strstr(name, "-report.json") != NULL ||
                        strstr(name, ".report") != NULL ||
                        (strstr(name, "report.json") != NULL) ||
                        (strstr(name, "report") != NULL && strstr(name, ".json") != NULL)) {
                        break;
                    }

                    // Only process events that have filenames (not just directory events)
                    // Check if this is a file modification event
                    if (event->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO)) {
                        // Skip directories
                        if (event->mask & IN_ISDIR) {
                            break;
                        }

                        // Check if this is a new change (avoid duplicate notifications)
                        char event_key[1024];
                        snprintf(event_key, sizeof(event_key), "%s/%s", entry->repository, event->name);

                        static char last_event_key[1024] = "";
                        static time_t last_event_time = 0;

                        if (strcmp(event_key, last_event_key) != 0 || now - last_event_time >= 1) {
                            write_change_notification(stream_file, event->name, entry->repository, now);
                            update_file_changes_report(report_file, event->name, entry->repository, now);
                            strncpy(last_event_key, event_key, sizeof(last_event_key) - 1);
                            last_event_time = now;
                        }
                    }
                    break;
                }
            }
        }
    }

    // Handle read errors (EAGAIN is expected for non-blocking)
    if (len < 0 && errno != EAGAIN) {
        fprintf(stderr, "Error reading inotify events: %s\n", strerror(errno));
    }
}

// Clean up expired entries from the stream file (older than 30 seconds)
void cleanup_expired_entries(const char* stream_file) {
    // Read all entries
    FILE* fp = fopen(stream_file, "r");
    if (!fp) {
        // File doesn't exist yet, nothing to clean up
        return;
    }

    // Structure to hold active entries
    typedef struct {
        char* line;
        time_t timestamp;
    } entry_t;

    entry_t* entries = NULL;
    size_t entry_count = 0;
    char line[4096];
    time_t now = time(NULL);

    // Read all lines and filter active ones
    while (fgets(line, sizeof(line), fp)) {
        // Parse JSON to extract timestamp
        json_value_t* json = json_parse_string(line);
        if (json && json->type == JSON_OBJECT) {
            json_value_t* timestamp_val = get_nested_value(json, "timestamp");
            if (timestamp_val && timestamp_val->type == JSON_NUMBER) {
                time_t timestamp = (time_t)timestamp_val->value.num_val;
                if (now - timestamp < 30) { // Keep entries less than 30 seconds old
                    entries = realloc(entries, (entry_count + 1) * sizeof(entry_t));
                    entries[entry_count].line = strdup(line);
                    entries[entry_count].timestamp = timestamp;
                    entry_count++;
                }
            }
        }
        if (json) json_free(json);
    }

    fclose(fp);

    // Write back active entries
    fp = fopen(stream_file, "w");
    if (!fp) {
        fprintf(stderr, "Failed to write cleaned stream file: %s\n", stream_file);
        // Cleanup
        for (size_t i = 0; i < entry_count; i++) {
            free(entries[i].line);
        }
        free(entries);
        return;
    }

    // Sort by timestamp (newest first) and write
    for (size_t i = 0; i < entry_count; i++) {
        fprintf(fp, "%s", entries[i].line);
        free(entries[i].line);
    }

    free(entries);
    fclose(fp);
}

// Get tracked files from a repository using git ls-files (excluding directories)
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

        // Skip directories (git ls-files can return directories)
        struct stat st;
        if (stat(line, &st) == 0 && S_ISDIR(st.st_mode)) {
            continue;
        }

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

// Setup inotify watches for all tracked files across all repositories
int setup_inotify_watches(watch_collection_t* watches) {
    if (!watches) return -1;

    // Read git-submodules.report to get all repositories
    fprintf(stderr, "Reading git-submodules.report...\n");
    json_value_t* submodules_report = json_parse_file("git-submodules.report");
    if (!submodules_report || submodules_report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load git-submodules.report\n");
        return -1;
    }
    fprintf(stderr, "Successfully loaded git-submodules.report\n");

    json_value_t* repos = get_nested_value(submodules_report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "No repositories found in git-submodules.report\n");
        json_free(submodules_report);
        return -1;
    }

    int watch_count = 0;

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

        // Use absolute path from git-submodules.report
        // Add watch for this repository directory (watch recursively by watching the repo root)
        if (add_directory_watch(watches, repo_path_str, repo_name_str) == 0) {
            watch_count++;
            fprintf(stderr, "Watching directory: %s (%s)\n", repo_path_str, repo_name_str);
        }
    }

    json_free(submodules_report);
    fprintf(stderr, "Set up %d inotify watches\n", watch_count);
    return watch_count;
}

int main(int argc, char* argv[]) {
    fprintf(stderr, "Starting file-changes-watcher daemon...\n");

    // Fork into background to run as daemon
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Failed to fork daemon process\n");
        return 1;
    }

    if (pid > 0) {
        // Parent process exits
        fprintf(stderr, "File-changes-watcher daemon started with PID %d\n", pid);
        return 0;
    }

    // Child process becomes daemon
    if (setsid() < 0) {
        fprintf(stderr, "Failed to create new session\n");
        return 1;
    }

    // Change to repoWatch directory
    if (chdir("..") < 0) {
        fprintf(stderr, "Failed to change to repoWatch directory\n");
        return 1;
    }

    // Set up signal handlers for clean shutdown
    struct sigaction sa;
    sa.sa_handler = daemon_signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Initialize watches
    g_watches = watch_collection_init();
    if (!g_watches) {
        fprintf(stderr, "Failed to initialize watch collection\n");
        return 1;
    }

    const char* stream_file = "three-pane-tui/file-changes-stream.json";
    const char* report_file = "file-changes-report.json";

    // Setup inotify watches for all tracked files
    int watch_count = setup_inotify_watches(g_watches);
    if (watch_count < 0) {
        fprintf(stderr, "Failed to setup inotify watches\n");
        watch_collection_cleanup(g_watches);
        return 1;
    }

    fprintf(stderr, "Set up %d inotify watches, daemon running...\n", watch_count);

    // Test inotify events immediately
    fprintf(stderr, "Testing inotify events...\n");
    process_inotify_events(g_watches, stream_file, report_file);

    // Main daemon loop
    int loop_count = 0;
    while (g_running) {
        loop_count++;
        if (loop_count % 100 == 0) { // Log every 10 seconds (100 * 100ms)
            fprintf(stderr, "Daemon loop running (iteration %d)...\n", loop_count);
        }

        // Process any pending inotify events
        process_inotify_events(g_watches, stream_file, report_file);

        // Clean up expired entries from stream file and report every 30 seconds
        static time_t last_cleanup = 0;
        time_t now = time(NULL);
        if (now - last_cleanup >= 30) {
            cleanup_expired_entries(stream_file);
            cleanup_expired_report_entries(report_file);
            last_cleanup = now;
        }

        // Sleep briefly to avoid busy waiting
        usleep(100000); // 100ms
    }

    fprintf(stderr, "File-changes-watcher daemon shutting down...\n");

    // Clean shutdown
    watch_collection_cleanup(g_watches);
    return 0;
}