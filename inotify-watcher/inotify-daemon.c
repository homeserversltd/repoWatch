#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <limits.h>
#include "inotify-daemon.h"
#include "../json-utils/json-utils.h"

// Global daemon state
daemon_state_t* g_daemon_state = NULL;

// Signal handler for report generation
void handle_sigusr1(int sig) {
    (void)sig;
    if (g_daemon_state) {
        g_daemon_state->should_write_report = 1;
    }
}

// Signal handler for cleanup and exit
void handle_sigterm(int sig) {
    (void)sig;
    if (g_daemon_state) {
        g_daemon_state->should_exit = 1;
    }
}

// Check if path should be excluded from watching
int should_exclude_path(const char* path) {
    if (!path) return 1;
    
    // Exclude .git directories
    if (strstr(path, "/.git/") != NULL || strstr(path, "/.git") == path + strlen(path) - 5) {
        return 1;
    }
    
    // Exclude common temporary/object files
    const char* patterns[] = {".tmp", ".swp", ".o", "~", ".cache"};
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        if (strstr(path, patterns[i]) != NULL) {
            return 1;
        }
    }
    
    return 0;
}

// Recursively add inotify watch to directory
int add_watch_recursive(const char* path, const char* repository) {
    if (!path || !repository || !g_daemon_state) return -1;
    
    // Check if should exclude
    if (should_exclude_path(path)) {
        return 0;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    
    // Only watch directories
    if (!S_ISDIR(st.st_mode)) {
        return 0;
    }
    
    // Add watch to this directory
    int wd = inotify_add_watch(g_daemon_state->inotify_fd, path,
                               IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    if (wd < 0) {
        if (errno == ENOSPC) {
            fprintf(stderr, "ERROR: inotify watch limit reached. Cannot add more watches.\n");
        }
        return -1;
    }
    
    // Add to watch mapping
    if (g_daemon_state->watch_count >= g_daemon_state->watch_capacity) {
        g_daemon_state->watch_capacity = g_daemon_state->watch_capacity == 0 ? 16 : g_daemon_state->watch_capacity * 2;
        watch_entry_t* new_watches = realloc(g_daemon_state->watches,
                                            g_daemon_state->watch_capacity * sizeof(watch_entry_t));
        if (!new_watches) {
            inotify_rm_watch(g_daemon_state->inotify_fd, wd);
            return -1;
        }
        g_daemon_state->watches = new_watches;
    }
    
    watch_entry_t* entry = &g_daemon_state->watches[g_daemon_state->watch_count];
    entry->wd = wd;
    entry->path = strdup(path);
    entry->repository = strdup(repository);
    g_daemon_state->watch_count++;
    
    // Recursively add watches to subdirectories
    DIR* dir = opendir(path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            char subpath[PATH_MAX];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
            
            if (should_exclude_path(subpath)) {
                continue;
            }
            
            struct stat subst;
            if (stat(subpath, &subst) == 0 && S_ISDIR(subst.st_mode)) {
                add_watch_recursive(subpath, repository);
            }
        }
        closedir(dir);
    }
    
    return 0;
}

// Get repository name from path
const char* get_repository_name(const char* repo_path) {
    // Extract repository name from path (last component)
    const char* name = strrchr(repo_path, '/');
    if (name) {
        return name + 1;
    }
    return repo_path;
}

// Initialize daemon with repository paths
int daemon_init(const char* git_submodules_report_path, const char* report_file_path) {
    if (!git_submodules_report_path || !report_file_path) {
        return -1;
    }
    
    // Allocate daemon state
    g_daemon_state = calloc(1, sizeof(daemon_state_t));
    if (!g_daemon_state) {
        return -1;
    }
    
    g_daemon_state->report_file = strdup(report_file_path);
    g_daemon_state->git_submodules_report = strdup(git_submodules_report_path);
    g_daemon_state->watch_capacity = 16;
    g_daemon_state->event_capacity = 100;
    g_daemon_state->watches = calloc(g_daemon_state->watch_capacity, sizeof(watch_entry_t));
    g_daemon_state->events = calloc(g_daemon_state->event_capacity, sizeof(file_event_t));
    
    if (!g_daemon_state->watches || !g_daemon_state->events) {
        daemon_cleanup();
        return -1;
    }
    
    // Initialize inotify
    g_daemon_state->inotify_fd = inotify_init();
    if (g_daemon_state->inotify_fd < 0) {
        perror("inotify_init");
        daemon_cleanup();
        return -1;
    }
    
    // Set up signal handlers
    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
    
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    
    // Parse git-submodules.report to get repository paths
    json_value_t* report = json_parse_file(git_submodules_report_path);
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to parse git-submodules.report\n");
        daemon_cleanup();
        return -1;
    }
    
    json_value_t* repos = get_nested_value(report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "No repositories array found in git-submodules.report\n");
        json_free(report);
        daemon_cleanup();
        return -1;
    }
    
    // Add watches for each repository
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo = repos->value.arr_val->items[i];
        if (repo->type != JSON_OBJECT) continue;
        
        json_value_t* repo_path = get_nested_value(repo, "path");
        json_value_t* repo_name = get_nested_value(repo, "name");
        
        if (repo_path && repo_path->type == JSON_STRING) {
            const char* path = repo_path->value.str_val;
            const char* name = "root";
            if (repo_name && repo_name->type == JSON_STRING) {
                name = repo_name->value.str_val;
            }
            
            // Resolve relative paths (relative to repoWatch root)
            char full_path[PATH_MAX];
            if (path[0] == '/') {
                strncpy(full_path, path, sizeof(full_path) - 1);
                full_path[sizeof(full_path) - 1] = '\0';
            } else {
                // Get repoWatch root directory (parent of inotify-watcher)
                char cwd[PATH_MAX];
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    // We're in inotify-watcher directory, go up one level
                    snprintf(full_path, sizeof(full_path), "%s/../%s", cwd, path);
                } else {
                    strncpy(full_path, path, sizeof(full_path) - 1);
                    full_path[sizeof(full_path) - 1] = '\0';
                }
            }
            
            // Normalize path (resolve .. and .)
            char resolved_path[PATH_MAX];
            if (realpath(full_path, resolved_path) == NULL) {
                // If realpath fails, use the original path
                strncpy(resolved_path, full_path, sizeof(resolved_path) - 1);
                resolved_path[sizeof(resolved_path) - 1] = '\0';
            }
            
            add_watch_recursive(resolved_path, name);
        }
    }
    
    json_free(report);
    
    fprintf(stderr, "Daemon initialized with %zu watches\n", g_daemon_state->watch_count);
    return 0;
}

// Find or create file event
file_event_t* find_or_create_event(const char* path, const char* repository, int event_type) {
    if (!g_daemon_state || !path || !repository) return NULL;
    
    time_t now = time(NULL);
    
    // Look for existing event
    for (size_t i = 0; i < g_daemon_state->event_count; i++) {
        if (strcmp(g_daemon_state->events[i].path, path) == 0 &&
            strcmp(g_daemon_state->events[i].repository, repository) == 0) {
            // Update existing event
            g_daemon_state->events[i].last_updated = now;
            g_daemon_state->events[i].event_type = event_type;
            return &g_daemon_state->events[i];
        }
    }
    
    // Create new event
    if (g_daemon_state->event_count >= g_daemon_state->event_capacity) {
        g_daemon_state->event_capacity *= 2;
        file_event_t* new_events = realloc(g_daemon_state->events,
                                          g_daemon_state->event_capacity * sizeof(file_event_t));
        if (!new_events) return NULL;
        g_daemon_state->events = new_events;
    }
    
    file_event_t* event = &g_daemon_state->events[g_daemon_state->event_count];
    event->path = strdup(path);
    event->repository = strdup(repository);
    event->timestamp = now;
    event->event_type = event_type;
    event->first_detected = now;
    event->last_updated = now;
    g_daemon_state->event_count++;
    
    return event;
}

// Get path from watch descriptor
const char* get_path_from_wd(int wd) {
    if (!g_daemon_state) return NULL;
    
    for (size_t i = 0; i < g_daemon_state->watch_count; i++) {
        if (g_daemon_state->watches[i].wd == wd) {
            return g_daemon_state->watches[i].path;
        }
    }
    return NULL;
}

// Get repository from watch descriptor
const char* get_repository_from_wd(int wd) {
    if (!g_daemon_state) return NULL;
    
    for (size_t i = 0; i < g_daemon_state->watch_count; i++) {
        if (g_daemon_state->watches[i].wd == wd) {
            return g_daemon_state->watches[i].repository;
        }
    }
    return NULL;
}

// Write report to file
void write_report(void) {
    if (!g_daemon_state || !g_daemon_state->report_file) return;
    
    // Create JSON report
    json_value_t* root = json_create_object();
    if (!root) return;
    
    json_object_set(root, "report_type", json_create_string("inotify_file_changes"));
    json_object_set(root, "generated_by", json_create_string("inotify-watcher"));
    json_object_set(root, "timestamp", json_create_number((double)time(NULL)));
    
    json_value_t* files_array = json_create_array();
    if (files_array) {
        for (size_t i = 0; i < g_daemon_state->event_count; i++) {
            file_event_t* event = &g_daemon_state->events[i];
            
            json_value_t* file_obj = json_create_object();
            if (file_obj) {
                json_object_set(file_obj, "path", json_create_string(event->path));
                json_object_set(file_obj, "repository", json_create_string(event->repository));
                json_object_set(file_obj, "first_detected", json_create_number((double)event->first_detected));
                json_object_set(file_obj, "last_updated", json_create_number((double)event->last_updated));
                
                // Convert event type to string
                const char* event_type_str = "UNKNOWN";
                if (event->event_type & IN_MODIFY) event_type_str = "IN_MODIFY";
                else if (event->event_type & IN_CREATE) event_type_str = "IN_CREATE";
                else if (event->event_type & IN_DELETE) event_type_str = "IN_DELETE";
                else if (event->event_type & IN_MOVED_FROM) event_type_str = "IN_MOVED_FROM";
                else if (event->event_type & IN_MOVED_TO) event_type_str = "IN_MOVED_TO";
                
                json_object_set(file_obj, "event_type", json_create_string(event_type_str));
                json_array_add(files_array, file_obj);
            }
        }
        json_object_set(root, "files", files_array);
    }
    
    // Write to temp file first, then rename (atomic write)
    char temp_file[PATH_MAX];
    snprintf(temp_file, sizeof(temp_file), "%s.tmp", g_daemon_state->report_file);
    
    if (json_write_file(temp_file, root) == 0) {
        // Atomic rename
        rename(temp_file, g_daemon_state->report_file);
    } else {
        unlink(temp_file);
    }
    
    json_free(root);
}

// Main daemon event loop
void daemon_run(void) {
    if (!g_daemon_state) return;
    
    char buffer[4096];
    
    while (!g_daemon_state->should_exit) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(g_daemon_state->inotify_fd, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int ready = select(g_daemon_state->inotify_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ready > 0 && FD_ISSET(g_daemon_state->inotify_fd, &read_fds)) {
            ssize_t length = read(g_daemon_state->inotify_fd, buffer, sizeof(buffer));
            
            if (length < 0) {
                if (errno != EINTR) {
                    perror("read");
                }
                continue;
            }
            
            // Process inotify events
            size_t i = 0;
            while (i < (size_t)length) {
                struct inotify_event* event = (struct inotify_event*)&buffer[i];
                
                if (event->len > 0) {
                    const char* watch_path = get_path_from_wd(event->wd);
                    const char* repository = get_repository_from_wd(event->wd);
                    
                    if (watch_path && repository) {
                        // Build full file path
                        char file_path[PATH_MAX];
                        snprintf(file_path, sizeof(file_path), "%s/%s", watch_path, event->name);
                        
                        struct stat st;
                        if (stat(file_path, &st) == 0) {
                            if (S_ISREG(st.st_mode)) {
                                // Regular file - track it
                                char rel_path[PATH_MAX];
                                // For now, use the full path - can be optimized later
                                strncpy(rel_path, file_path, sizeof(rel_path) - 1);
                                rel_path[sizeof(rel_path) - 1] = '\0';
                                
                                find_or_create_event(rel_path, repository, event->mask);
                            } else if (S_ISDIR(st.st_mode) && (event->mask & IN_CREATE)) {
                                // New directory created - add watch to it
                                add_watch_recursive(file_path, repository);
                            }
                        }
                    }
                }
                
                i += sizeof(struct inotify_event) + event->len;
            }
        }
        
        // Check if we should write report
        if (g_daemon_state->should_write_report) {
            write_report();
            g_daemon_state->should_write_report = 0;
        }
    }
}

// Cleanup and exit
void daemon_cleanup(void) {
    if (!g_daemon_state) return;
    
    // Remove all watches
    if (g_daemon_state->inotify_fd >= 0) {
        for (size_t i = 0; i < g_daemon_state->watch_count; i++) {
            inotify_rm_watch(g_daemon_state->inotify_fd, g_daemon_state->watches[i].wd);
            free(g_daemon_state->watches[i].path);
            free(g_daemon_state->watches[i].repository);
        }
        close(g_daemon_state->inotify_fd);
    }
    
    // Free events
    for (size_t i = 0; i < g_daemon_state->event_count; i++) {
        free(g_daemon_state->events[i].path);
        free(g_daemon_state->events[i].repository);
    }
    
    free(g_daemon_state->watches);
    free(g_daemon_state->events);
    free(g_daemon_state->report_file);
    free(g_daemon_state->git_submodules_report);
    free(g_daemon_state);
    g_daemon_state = NULL;
}

// Main daemon entry point
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    // Daemonize: fork and detach from terminal
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    
    if (pid > 0) {
        // Parent process - exit
        return 0;
    }
    
    // Child process - become session leader
    setsid();
    
    // Second fork to ensure we're not a session leader
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    
    if (pid > 0) {
        // Parent process - exit
        return 0;
    }
    
    // Change to repoWatch root directory
    // The daemon is executed from inotify-watcher directory, so go up one level
    if (chdir("..") != 0) {
        // If that fails, try to find repoWatch root another way
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            // Look for git-submodules.report in current or parent directory
            if (access("git-submodules.report", R_OK) != 0) {
                if (access("../git-submodules.report", R_OK) == 0) {
                    chdir("..");
                }
            }
        }
    }
    
    // Redirect stdout/stderr to /dev/null
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    
    // Initialize daemon
    if (daemon_init("git-submodules.report", "inotify-changes-report.json") != 0) {
        return 1;
    }
    
    // Run daemon
    daemon_run();
    
    // Write final report before exit
    write_report();
    
    // Cleanup
    daemon_cleanup();
    
    return 0;
}
