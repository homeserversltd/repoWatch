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

// Enable mouse reporting for X11 xterm mouse protocol
int enable_mouse_reporting() {
    // Enable X11 xterm mouse reporting (button press/release, wheel)
    printf("\033[?1000h"); // Basic mouse reporting
    printf("\033[?1002h"); // Button event mouse reporting
    printf("\033[?1003h"); // All motion mouse reporting
    printf("\033[?1006h"); // SGR mouse mode (extended coordinates)
    fflush(stdout);
    return 0;
}

// Disable mouse reporting
void disable_mouse_reporting() {
    printf("\033[?1000l"); // Disable basic mouse reporting
    printf("\033[?1002l"); // Disable button event mouse reporting
    printf("\033[?1003l"); // Disable all motion mouse reporting
    printf("\033[?1006l"); // Disable SGR mouse mode
    fflush(stdout);
}

// Read mouse event from stdin
// Returns: 0 on success, -1 on no data, -2 on invalid data, -3 on incomplete data
int read_mouse_event(int* button, int* x, int* y, int* scroll_delta) {
    unsigned char buf[16]; // Smaller buffer, mouse events are typically 6-8 bytes
    int n = read(STDIN_FILENO, buf, sizeof(buf));

    if (n <= 0) return -1; // No data

    // Check for SGR mouse mode: \e[<button;x;yM or \e[<button;x;ym
    if (n >= 6 && buf[0] == '\033' && buf[1] == '[' && buf[2] == '<') {
        // Find the end of the mouse event (M or m)
        int event_end = -1;
        for (int i = 3; i < n; i++) {
            if (buf[i] == 'M' || buf[i] == 'm') {
                event_end = i;
                break;
            }
        }

        if (event_end == -1) {
            // Incomplete mouse event, put bytes back if possible
            // For now, just return incomplete
            return -3;
        }

        // Parse the complete mouse event
        char* endptr;
        int b = strtol((char*)buf + 3, &endptr, 10);
        if (*endptr != ';') return -2;

        int px = strtol(endptr + 1, &endptr, 10);
        if (*endptr != ';') return -2;

        int py = strtol(endptr + 1, &endptr, 10);
        if (*endptr != 'M' && *endptr != 'm') return -2;

        *x = px;
        *y = py;

        // Parse button information
        *scroll_delta = 0;
        if (b & 64) { // Scroll wheel
            *scroll_delta = (b & 1) ? 1 : -1; // 64+1 = scroll down, 64+0 = scroll up
            *button = 0; // No button pressed for scroll
        } else {
            *button = b & 3; // Button number (0=left, 1=middle, 2=right)
            if (b & 32) *button = -1; // Button release
        }

        // If we read more than one mouse event, the extra bytes remain in buffer
        // This is handled by the main loop calling us repeatedly

        return 0; // Success
    }

    return -2; // Invalid format
}

// Read single character with timeout
// Returns: character on success, -1 on no data, -2 on error
int read_char_timeout() {
    unsigned char c;
    int n = read(STDIN_FILENO, &c, 1);
    if (n == 1) return c;
    if (n == 0) return -1; // No data
    return -2; // Error
}
