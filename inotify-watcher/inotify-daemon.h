#ifndef INOTIFY_DAEMON_H
#define INOTIFY_DAEMON_H

#include <sys/inotify.h>
#include <time.h>

// Event tracking structure
typedef struct {
    char* path;
    char* repository;
    time_t timestamp;
    int event_type;  // IN_MODIFY, IN_CREATE, IN_DELETE, etc.
    time_t first_detected;
    time_t last_updated;
} file_event_t;

// Watch descriptor mapping
typedef struct {
    int wd;
    char* path;
    char* repository;
} watch_entry_t;

// Daemon state structure
typedef struct {
    int inotify_fd;
    watch_entry_t* watches;
    size_t watch_count;
    size_t watch_capacity;
    file_event_t* events;
    size_t event_count;
    size_t event_capacity;
    volatile int should_write_report;
    volatile int should_exit;
    char* report_file;
    char* git_submodules_report;
} daemon_state_t;

// Function declarations
int daemon_init(const char* git_submodules_report_path, const char* report_file_path);
int add_watch_recursive(const char* path, const char* repository);
void daemon_run(void);
void handle_sigusr1(int sig);
void handle_sigterm(int sig);
void write_report(void);
void daemon_cleanup(void);
int should_exclude_path(const char* path);

// Global daemon state
extern daemon_state_t* g_daemon_state;

#endif // INOTIFY_DAEMON_H
