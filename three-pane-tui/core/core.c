#include "../three-pane-tui.h"

// Global flag for redraw requests (defined in main module)
extern volatile sig_atomic_t redraw_needed;

// Signal handler for window resize
void handle_sigwinch(int sig) {
    redraw_needed = 1;
}

// Terminal control functions
void save_cursor_position() {
    printf("\033[s");
}

void restore_cursor_position() {
    printf("\033[u");
}

void hide_cursor() {
    printf("\033[?25l");
}

void show_cursor() {
    printf("\033[?25h");
}

void clear_screen() {
    printf("\033[2J");
}

void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

void reset_colors() {
    printf("\033[0m");
}

void set_bold() {
    printf("\033[1m");
}

void set_color(int color_code) {
    printf("\033[%dm", color_code);
}

void set_background(int color_code) {
    printf("\033[%dm", color_code + 10); // Background colors are 40-47, foreground are 30-37
}

// Get terminal size
int get_terminal_size(int* width, int* height) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        char* columns = getenv("COLUMNS");
        char* lines = getenv("LINES");

        if (columns && lines) {
            *width = atoi(columns);
            *height = atoi(lines);
            return 0;
        }

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
