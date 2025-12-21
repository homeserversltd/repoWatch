#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <time.h>
#include <signal.h>

// Configuration structure
typedef struct {
    char* title;
    char* status_source;
    int display_duration;
    char* exit_keys;
    int centered_text;
    int auto_refresh;
    int refresh_interval;
} config_t;

// Global state
volatile sig_atomic_t interrupted = 0;

// Signal handler for clean exit
void signal_handler(int signum) {
    interrupted = 1;
}

// Load configuration from environment or defaults
config_t* load_config() {
    config_t* config = calloc(1, sizeof(config_t));
    if (!config) return NULL;

    const char* title_env = getenv("GIT_TUI_TITLE");
    config->title = title_env ? strdup(title_env) : strdup("repoWatch - Git Status Monitor");

    const char* source_env = getenv("GIT_TUI_STATUS_SOURCE");
    config->status_source = source_env ? strdup(source_env) : strdup("../git-status/.report");

    config->display_duration = 5000; // 5 seconds
    config->exit_keys = strdup("qQeE");
    config->centered_text = 0;
    config->auto_refresh = 1;
    config->refresh_interval = 2000; // 2 seconds

    return config;
}

// Get terminal size
void get_terminal_size(int* width, int* height) {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    *width = w.ws_col;
    *height = w.ws_row;
}

// Clear screen
void clear_screen() {
    printf("\033[2J\033[H");
}

// Move cursor to position
void move_cursor(int x, int y) {
    printf("\033[%d;%dH", y, x);
}

// Read status report from file
char* read_status_report(const char* filepath) {
    FILE* fp = fopen(filepath, "r");
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

// Display status in TUI format
void display_status(const char* title, const char* status_content, int term_width, int term_height) {
    clear_screen();

    // Draw border
    printf("┌");
    for (int i = 0; i < term_width - 2; i++) printf("─");
    printf("┐\n");

    // Title bar
    printf("│");
    int title_len = strlen(title);
    int title_start = (term_width - 2 - title_len) / 2;
    for (int i = 0; i < title_start; i++) printf(" ");
    printf("%s", title);
    for (int i = title_start + title_len; i < term_width - 2; i++) printf(" ");
    printf("│\n");

    // Separator
    printf("├");
    for (int i = 0; i < term_width - 2; i++) printf("─");
    printf("┤\n");

    // Content area
    if (status_content && strlen(status_content) > 0) {
        char* content_copy = strdup(status_content);
        char* line = strtok(content_copy, "\n");
        int line_count = 0;

        while (line && line_count < term_height - 6) { // Reserve space for borders and footer
            printf("│ %-*s │\n", term_width - 4, line);
            line = strtok(NULL, "\n");
            line_count++;
        }

        free(content_copy);
    } else {
        printf("│ %-*s │\n", term_width - 4, "No status information available");
        printf("│ %-*s │\n", term_width - 4, "Waiting for git-status child to run...");
    }

    // Fill remaining space
    for (int i = 0; i < term_height - 5 - (status_content ? 1 : 2); i++) {
        printf("│ %-*s │\n", term_width - 4, "");
    }

    // Footer
    printf("├");
    for (int i = 0; i < term_width - 2; i++) printf("─");
    printf("┤\n");

    printf("│ Press Q to quit, E to exit");
    int footer_used = 24;
    for (int i = footer_used; i < term_width - 2; i++) printf(" ");
    printf("│\n");

    // Bottom border
    printf("└");
    for (int i = 0; i < term_width - 2; i++) printf("─");
    printf("┘\n");

    fflush(stdout);
}

// Setup terminal for raw input
void setup_terminal() {
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;

    // Disable canonical mode and echo
    new_tio.c_lflag &= (~ICANON & ~ECHO);

    // Set the new settings
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

// Restore terminal settings
void restore_terminal() {
    struct termios tio;
    tcgetattr(STDIN_FILENO, &tio);
    tio.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &tio);
}

// Check if key is exit key
int is_exit_key(char key, const char* exit_keys) {
    for (size_t i = 0; i < strlen(exit_keys); i++) {
        if (key == exit_keys[i]) return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    printf("Git Status TUI starting...\n");

    config_t* config = load_config();
    if (!config) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }

    // Setup signal handler
    signal(SIGINT, signal_handler);

    // Setup terminal
    setup_terminal();

    printf("TUI initialized. Press Q to quit.\n");

    int term_width, term_height;
    get_terminal_size(&term_width, &term_height);

    time_t last_refresh = 0;
    char* last_status = NULL;

    while (!interrupted) {
        time_t now = time(NULL);

        // Check if we need to refresh
        if (config->auto_refresh &&
            (now - last_refresh >= config->refresh_interval / 1000)) {

            char* status_content = read_status_report(config->status_source);
            if (status_content) {
                // Only update display if status changed
                if (!last_status || strcmp(status_content, last_status) != 0) {
                    display_status(config->title, status_content, term_width, term_height);
                    free(last_status);
                    last_status = status_content;
                } else {
                    free(status_content);
                }
                last_refresh = now;
            }
        }

        // Check for keyboard input (non-blocking)
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms timeout

        int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ready > 0) {
            char key;
            if (read(STDIN_FILENO, &key, 1) > 0) {
                if (is_exit_key(key, config->exit_keys)) {
                    break;
                }
            }
        }
    }

    // Cleanup
    clear_screen();
    restore_terminal();

    free(last_status);
    free(config->title);
    free(config->status_source);
    free(config->exit_keys);
    free(config);

    printf("Git Status TUI exited.\n");
    return 0;
}
