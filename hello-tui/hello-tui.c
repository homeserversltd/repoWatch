#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <fcntl.h>

// Configuration for hello-tui module
typedef struct {
    char* title;
    int display_duration;
    char* exit_keys;
    int centered_text;
} hello_tui_config_t;

// Global flag for redraw requests
volatile sig_atomic_t redraw_needed = 0;

// Signal handler for window resize
void handle_sigwinch(int sig) {
    redraw_needed = 1;
}

// Orchestrator for hello-tui module
typedef struct {
    char* module_path;
    hello_tui_config_t config;
} hello_tui_orchestrator_t;

// Terminal control functions
void save_cursor_position() {
    printf("\033[s"); // Save cursor position
}

void restore_cursor_position() {
    printf("\033[u"); // Restore cursor position
}

void hide_cursor() {
    printf("\033[?25l"); // Hide cursor
}

void show_cursor() {
    printf("\033[?25h"); // Show cursor
}

void clear_screen() {
    printf("\033[2J"); // Clear entire screen
}

void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col); // Move cursor to row, col
}

void reset_colors() {
    printf("\033[0m"); // Reset all formatting
}

void set_bold() {
    printf("\033[1m"); // Bold text
}

void set_color(int color_code) {
    printf("\033[%dm", color_code); // Set text color
}

// Get terminal size
int get_terminal_size(int* width, int* height) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        // Fallback to environment variables
        char* columns = getenv("COLUMNS");
        char* lines = getenv("LINES");

        if (columns && lines) {
            *width = atoi(columns);
            *height = atoi(lines);
            return 0;
        }

        // Last resort defaults
        *width = 80;
        *height = 24;
        return -1;
    }

    *width = ws.ws_col;
    *height = ws.ws_row;
    return 0;
}

// Simple environment variable expansion
char* expandvars(const char* input) {
    if (!input) return NULL;
    return strdup(input);
}

// Load configuration from index.json (simplified hardcoded for demo)
int load_config(hello_tui_orchestrator_t* orch) {
    orch->config.title = expandvars("repoWatch TUI Demo");
    orch->config.display_duration = 2000;
    orch->config.exit_keys = strdup("qQ");
    orch->config.centered_text = 1; // true
    return 0;
}

// Initialize orchestrator
hello_tui_orchestrator_t* hello_tui_init(const char* module_path) {
    hello_tui_orchestrator_t* orch = calloc(1, sizeof(hello_tui_orchestrator_t));
    if (!orch) return NULL;

    orch->module_path = strdup(module_path);
    if (!orch->module_path) {
        free(orch);
        return NULL;
    }

    if (load_config(orch) != 0) {
        free(orch->module_path);
        free(orch);
        return NULL;
    }

    return orch;
}

// Cleanup orchestrator
void hello_tui_cleanup(hello_tui_orchestrator_t* orch) {
    if (orch) {
        free(orch->config.title);
        free(orch->config.exit_keys);
        free(orch->module_path);
        free(orch);
    }
}

// Draw the TUI overlay
void draw_tui_overlay(hello_tui_orchestrator_t* orch) {
    int width, height;
    get_terminal_size(&width, &height);

    clear_screen();
    move_cursor(1, 1);

    // Draw border
    set_color(36); // Cyan color
    set_bold();

    // Top border
    printf("┌");
    for (int i = 0; i < width - 2; i++) printf("─");
    printf("┐\n");

    // Side borders with content
    for (int row = 2; row < height; row++) {
        printf("│");
        if (row == height / 2 - 2) {
            // Title line
            int title_len = strlen(orch->config.title);
            int padding = (width - 2 - title_len) / 2;
            for (int i = 0; i < padding; i++) printf(" ");
            printf("%s", orch->config.title);
            for (int i = 0; i < width - 2 - padding - title_len; i++) printf(" ");
        } else if (row == height / 2) {
            // Hello World line
            const char* hello = "Hello World";
            int hello_len = strlen(hello);
            int padding = (width - 2 - hello_len) / 2;
            for (int i = 0; i < padding; i++) printf(" ");
            printf("%s", hello);
            for (int i = 0; i < width - 2 - padding - hello_len; i++) printf(" ");
        } else if (row == height / 2 + 2) {
            // Instructions line
            const char* instr = "Press Q to exit";
            int instr_len = strlen(instr);
            int padding = (width - 2 - instr_len) / 2;
            for (int i = 0; i < padding; i++) printf(" ");
            printf("%s", instr);
            for (int i = 0; i < width - 2 - padding - instr_len; i++) printf(" ");
        } else if (row == height - 2) {
            // Status line
            char status[256];
            snprintf(status, sizeof(status), "Terminal: %dx%d", width, height);
            int status_len = strlen(status);
            int padding = (width - 2 - status_len) / 2;
            for (int i = 0; i < padding; i++) printf(" ");
            printf("%s", status);
            for (int i = 0; i < width - 2 - padding - status_len; i++) printf(" ");
        } else {
            // Empty line
            for (int i = 0; i < width - 2; i++) printf(" ");
        }
        printf("│\n");
    }

    // Bottom border
    printf("└");
    for (int i = 0; i < width - 2; i++) printf("─");
    printf("┘\n");

    reset_colors();
    fflush(stdout);
}

// Execute the hello-tui module
int hello_tui_execute(hello_tui_orchestrator_t* orch) {
    // Set up signal handler for window resize
    struct sigaction sa;
    sa.sa_handler = handle_sigwinch;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    // Save current terminal state
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;

    // Disable canonical mode and echo for immediate key input
    new_tio.c_lflag &= (~ICANON & ~ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    // Make stdin non-blocking
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    // Hide cursor and save position
    hide_cursor();
    save_cursor_position();

    // Draw the initial TUI overlay
    draw_tui_overlay(orch);

    // Main input loop
    int running = 1;
    while (running) {
        // Check for redraw request from signal handler
        if (redraw_needed) {
            redraw_needed = 0;
            draw_tui_overlay(orch);
        }

        // Check for keyboard input (non-blocking)
        int c = getchar();
        if (c != EOF) {
            // Check for exit keys
            if (c == 'q' || c == 'Q' || c == 27) { // 27 is Escape
                running = 0;
            }
        }

        // Small delay to prevent busy waiting
        usleep(10000); // 10ms
    }

    // Cleanup: restore terminal state
    clear_screen();
    restore_cursor_position();
    show_cursor();

    // Restore blocking mode
    fcntl(STDIN_FILENO, F_SETFL, flags);

    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

    return 0;
}

int main(int argc, char* argv[]) {
    // Get the module path
    char module_path[1024];
    if (!getcwd(module_path, sizeof(module_path))) {
        fprintf(stderr, "Error: Cannot get current working directory\n");
        return 1;
    }

    // Initialize hello-tui orchestrator
    hello_tui_orchestrator_t* orch = hello_tui_init(module_path);
    if (!orch) {
        fprintf(stderr, "Error: Failed to initialize hello-tui orchestrator\n");
        return 1;
    }

    // Execute hello-tui module
    int result = hello_tui_execute(orch);

    // Cleanup
    hello_tui_cleanup(orch);

    return result;
}
