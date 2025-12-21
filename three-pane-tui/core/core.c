#include "../three-pane-tui.h"
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <curses.h>

// Global flag for redraw requests (defined in main module)
extern volatile sig_atomic_t redraw_needed;

// Signal handler for window resize
void handle_sigwinch(int sig) {
    redraw_needed = 1;
}

// Emergency cleanup handler for crash signals
void emergency_cleanup(int sig) {
    // Restore terminal state in case of crash
    disable_mouse_reporting();
    show_cursor();
    clear_screen();
    move_cursor(1, 1);
    printf("Program terminated unexpectedly (signal %d)\n", sig);
    fflush(stdout);

    // Exit with the signal number
    _exit(128 + sig);
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
    // Only enable necessary modes - avoid all motion reporting which is too aggressive
    printf("\033[?1000h"); // Basic mouse reporting (button press/release)
    printf("\033[?1002h"); // Button event mouse reporting (drag events)
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

    // First, peek at 1 byte to see if this could be a mouse event
    int n = read(STDIN_FILENO, buf, 1);
    if (n <= 0) return -1; // No data

    // Mouse events always start with \033 (escape)
    if (buf[0] != '\033') {
        return -2; // Not a mouse event, don't consume more
    }

    // Read 2 more bytes to check for [< pattern
    n = read(STDIN_FILENO, buf + 1, 2);
    if (n < 2) return -3; // Incomplete

    // Check if this looks like a mouse event start: \e[<
    if (buf[1] == '[' && buf[2] == '<') {
        // This looks like a mouse event, read the rest
        int remaining = read(STDIN_FILENO, buf + 3, sizeof(buf) - 3);
        if (remaining < 0) return -3; // Error reading rest
        n = 3 + remaining;

        // Now we have the complete mouse event
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

// Measure the display width of a UTF-8 string (simplified UTF-8 aware approach)
int get_string_display_width(const char* str) {
    if (!str || !*str) return 0;

    // For now, use a simplified approach: count bytes but account for basic UTF-8
    // This is not perfect for wide characters like emojis, but better than character counting
    int width = 0;
    const char* p = str;

    while (*p) {
        unsigned char c = *p;
        if (c < 128) {
            // ASCII character - 1 width
            width += 1;
            p += 1;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8 sequence - assume 1 width (most common case)
            width += 1;
            p += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8 sequence - assume 1 width (most common case)
            width += 1;
            p += 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte UTF-8 sequence - could be 2 width (emojis), assume 1 for now
            width += 1;
            p += 4;
        } else {
            // Invalid UTF-8, count as 1
            width += 1;
            p += 1;
        }
    }

    return width;
}

// Smart truncation: for paths, show first folder + ... + filename, for others use right-priority
char* truncate_string_right_priority(const char* str, int max_width) {
    if (!str) return NULL;

    int ellipses_width = 3; // Width of "..."
    int total_width = get_string_display_width(str);

    // If it fits, return copy as-is
    if (total_width <= max_width) {
        return strdup(str);
    }

    // Check if this looks like a path (contains slashes)
    const char* first_slash = strchr(str, '/');
    if (first_slash) {
        // Smart path truncation: expand from root as far right as possible, then .../filename
        const char* last_slash = strrchr(str, '/');
        if (last_slash && last_slash > first_slash) {
            // Extract filename (from last slash to end)
            const char* filename = last_slash + 1;

            // Build path components array
            char* path_copy = strdup(str);
            char* components[100]; // Max 100 components should be plenty
            int component_count = 0;

            char* token = strtok(path_copy, "/");
            while (token && component_count < 99) {
                components[component_count++] = token;
                token = strtok(NULL, "/");
            }

            if (component_count >= 2) { // Need at least root folder + filename
                // Build path from left to right, checking width
                char* current_path = calloc(strlen(str) + ellipses_width + 2, sizeof(char)); // +2 for slashes
                int current_pos = 0;

                // Always include root folder
                strcpy(current_path, components[0]);
                current_pos = 1;

                // Add as many middle components as possible
                for (int i = 1; i < component_count - 1; i++) { // Skip root (0) and filename (last)
                    // Try adding this component
                    char* test_path = calloc(strlen(current_path) + strlen(components[i]) + 2, sizeof(char));
                    sprintf(test_path, "%s/%s", current_path, components[i]);

                    // Add .../filename to test full width
                    char* full_test = calloc(strlen(test_path) + ellipses_width + strlen(filename) + 3, sizeof(char));
                    sprintf(full_test, "%s/.../%s", test_path, filename);

                    int test_width = get_string_display_width(full_test);
                    if (test_width <= max_width) {
                        // It fits, keep this component
                        strcpy(current_path, test_path);
                        current_pos = i + 1;
                        free(test_path);
                        free(full_test);
                    } else {
                        // Doesn't fit, stop here
                        free(test_path);
                        free(full_test);
                        break;
                    }
                }

                // Construct final result: path_so_far/.../filename
                char* smart_result = calloc(strlen(current_path) + ellipses_width + strlen(filename) + 3, sizeof(char));
                sprintf(smart_result, "%s/.../%s", current_path, filename);

                free(current_path);
                free(path_copy);

                // Final width check
                int smart_width = get_string_display_width(smart_result);
                if (smart_width <= max_width) {
                    return smart_result;
                }

                free(smart_result);
            } else {
                free(path_copy);
            }
        }
    }

    // Fall back to right-priority truncation
    // Available width for actual content (after ellipses)
    int available_width = max_width - ellipses_width;
    if (available_width <= 0) {
        // Not even room for ellipses, return minimal version
        return strdup("...");
    }

    // Find the rightmost characters that fit within available_width
    // Use UTF-8 aware approach: work backwards through the string
    int current_width = 0;
    size_t bytes_to_keep = 0;
    const char* ptr = str + strlen(str); // Start at end

    while (ptr > str && bytes_to_keep < strlen(str)) {
        // Move back one UTF-8 character
        ptr--;
        while (ptr > str && ((*ptr & 0xC0) == 0x80)) {
            // Continue back through continuation bytes
            ptr--;
        }

        // Calculate width of this character (simplified UTF-8 approach)
        const char* char_start = ptr;
        int char_width = 1; // Default width

        unsigned char first_byte = *char_start;
        if (first_byte < 128) {
            // ASCII - 1 width
            char_width = 1;
        } else if ((first_byte & 0xE0) == 0xC0) {
            // 2-byte sequence - typically 1 width
            char_width = 1;
        } else if ((first_byte & 0xF0) == 0xE0) {
            // 3-byte sequence - typically 1 width
            char_width = 1;
        } else if ((first_byte & 0xF8) == 0xF0) {
            // 4-byte sequence - could be 2 width (emojis), but assume 1
            char_width = 1;
        }

        int char_bytes = 0;
        const char* temp = char_start;
        while (*temp && char_bytes < 4) {
            if ((*temp & 0xC0) != 0x80 || char_bytes == 0) {
                char_bytes++;
            } else {
                break;
            }
            temp++;
            if (char_bytes >= 4) break; // Safety limit
        }

        if (current_width + char_width <= available_width) {
            current_width += char_width;
            bytes_to_keep = strlen(str) - (char_start - str);
        } else {
            break;
        }
    }

    // Create result with ellipses + rightmost characters
    char* result = NULL;
    if (bytes_to_keep > 0) {
        result = calloc(ellipses_width + bytes_to_keep + 1, sizeof(char));
        strcpy(result, "...");
        // Copy the rightmost bytes_to_keep bytes from original string
        const char* src_start = str + (strlen(str) - bytes_to_keep);
        strcpy(result + ellipses_width, src_start);
    } else {
        result = strdup("...");
    }

    return result;
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
