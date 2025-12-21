#include "../three-pane-tui.h"
#include <unistd.h>

// Update scroll state based on viewport and content
void update_scroll_state(pane_scroll_state_t* scroll_state, int viewport_height, int total_items) {
    scroll_state->viewport_height = viewport_height;
    scroll_state->total_items = total_items;

    if (total_items <= viewport_height) {
        scroll_state->max_scroll = 0;
        scroll_state->scroll_position = 0;
    } else {
        scroll_state->max_scroll = total_items - viewport_height;
        if (scroll_state->scroll_position > scroll_state->max_scroll) {
            scroll_state->scroll_position = scroll_state->max_scroll;
        }
    }
}

// Update pane scroll position
void update_pane_scroll(pane_scroll_state_t* scroll_state, int direction) {
    if (!scroll_state) return;

    if (direction > 0) {
        // Scroll down
        if (scroll_state->scroll_position < scroll_state->max_scroll) {
            scroll_state->scroll_position++;
        }
    } else if (direction < 0) {
        // Scroll up
        if (scroll_state->scroll_position > 0) {
            scroll_state->scroll_position--;
        }
    }

    // Ensure scroll position stays within bounds
    if (scroll_state->scroll_position < 0) {
        scroll_state->scroll_position = 0;
    }
    if (scroll_state->scroll_position > scroll_state->max_scroll) {
        scroll_state->scroll_position = scroll_state->max_scroll;
    }
}

// Determine which pane contains the given coordinates
int get_pane_at_position(int x, int y, int pane_width, int total_width, int pane_height) {
    // Safety checks
    if (pane_width <= 0 || total_width <= 0 || pane_height <= 0) {
        return 0;
    }

    // Check if click is within pane content area (below title row, above footer)
    if (y < 3 || y > 3 + pane_height) {
        return 0; // Outside pane content area
    }

    if (x < 0 || x >= total_width) return 0;

    if (x < pane_width) {
        return 1; // Left pane
    } else if (x < pane_width * 2) {
        return 2; // Center pane
    } else {
        return 3; // Right pane
    }

    return 0; // Outside panes
}

// Draw a single pane with scroll support
void draw_pane(int start_col, int width, int height, const char* title, char** items, size_t item_count, int title_color, const style_config_t* styles, int pane_index, const pane_scroll_state_t* scroll_state) {
    // Safety checks
    if (!title || !items || !styles || width <= 0 || height <= 0) {
        return;
    }

    // Draw title at the top of the pane (row 3, since row 1 is main title, row 2 is header separator)
    move_cursor(3, start_col);
    set_color(title_color);
    set_bold();
    printf("%s", title);
    reset_colors();

    // Draw items starting from row 4 (right after pane title and header separator)
    // height parameter is the available rows for content (from row 4 onwards)
    int current_row = 4;
    int max_row = 3 + height; // height is available content rows, so max is row 3 + height

    // Additional safety check
    if (height <= 0 || max_row < current_row) {
        return;
    }

    // Calculate which items to show based on scroll position
    size_t start_item = scroll_state ? scroll_state->scroll_position : 0;
    size_t end_item = start_item + height;

    if (end_item > item_count) {
        end_item = item_count;
    }

    // Draw visible items only
    for (size_t i = start_item; i < end_item && current_row <= max_row; i++) {
        move_cursor(current_row, start_col);
        // Apply color based on file type
        int file_color = get_file_color(items[i], styles);
        set_color(file_color);

        // Smart truncation prioritizing filename over directory path
        const char* text = items[i];
        int max_text_width = width; // Fill the pane completely

        // For pane 1 (tree view), allow much longer names before truncating
        if (pane_index == 1) {
            max_text_width = 256; // Allow full file names in tree view
        }

        // Use glyph-aware right-priority truncation for all content
        char* display_text = truncate_string_right_priority(text, max_text_width);
        printf("%s", display_text);
        free(display_text);
        reset_colors();
        current_row++;
    }

    // Draw scroll indicators if content is scrollable
    if (scroll_state && scroll_state->max_scroll > 0) {
        // Draw up arrow if not at top
        if (scroll_state->scroll_position > 0) {
            move_cursor(4, start_col + width - 1);
            set_color(32); // Green
            printf("↑");
        }

        // Draw down arrow if not at bottom
        if (scroll_state->scroll_position < scroll_state->max_scroll) {
            move_cursor(3 + height, start_col + width - 1);
            set_color(32); // Green
            printf("↓");
        }
    }
}

// Draw the three-pane TUI overlay
void draw_tui_overlay(three_pane_tui_orchestrator_t* orch) {
    if (!orch) return;

    int width, height;
    get_terminal_size(&width, &height);

    // Safety checks for minimum terminal size
    if (width < 20 || height < 10) {
        clear_screen();
        move_cursor(1, 1);
        printf("Terminal too small. Minimum size: 20x10");
        fflush(stdout);
        return;
    }

    clear_screen();

    // Main title at the very top
    move_cursor(1, 1);
    set_color(orch->config.styles.ui.title_color);
    set_bold();
    const char* view_name = (orch->current_view == VIEW_FLAT) ? "FLAT" : "TREE";
    printf("%s (%s)", orch->config.title, view_name);
    reset_colors();

    // Horizontal line under the header
    move_cursor(2, 1);
    set_color(orch->config.styles.ui.header_separator_color);
    for (int i = 0; i < width; i++) {
        printf("─");
    }
    reset_colors();

    // Calculate pane dimensions to maximize screen real estate
    int pane_width = width / 3;
    int remaining_width = width % 3; // Handle case where width is not divisible by 3
    int pane_height = height - 5; // Available rows: total height minus main title, header separator, pane titles, footer separator, and footer text

    // Draw vertical border lines between panes
    set_color(orch->config.styles.ui.borders.vertical);
    for (int row = 3; row <= height - 2; row++) { // From row 3 to row before footer separator
        // Border between pane 1 and 2
        move_cursor(row, pane_width);
        printf("│");

        // Border between pane 2 and 3
        move_cursor(row, pane_width * 2);
        printf("│");
    }
    reset_colors();

    // Horizontal line above the footer
    move_cursor(height - 1, 1);
    set_color(32); // Green for footer separator
    for (int i = 0; i < width; i++) {
        printf("─");
    }
    reset_colors();

    // Draw three panes side by side, maximizing screen space
    // Each pane starts at row 2 (below the main title)
    draw_pane(1, pane_width - 1, pane_height, orch->config.pane1_title,
              orch->data.pane1_items, orch->data.pane1_count, orch->config.styles.ui.pane_titles.left, &orch->config.styles, 1, &orch->data.pane1_scroll);

    draw_pane(pane_width + 1, pane_width - 1, pane_height, orch->config.pane2_title,
              orch->data.pane2_items, orch->data.pane2_count, orch->config.styles.ui.pane_titles.center, &orch->config.styles, 2, &orch->data.pane2_scroll);

    // Rightmost pane gets any remaining width minus the border
    draw_pane(pane_width * 2 + 1, pane_width + remaining_width - 1, pane_height, orch->config.pane3_title,
              orch->data.pane3_items, orch->data.pane3_count, orch->config.styles.ui.pane_titles.right, &orch->config.styles, 3, &orch->data.pane3_scroll);

    // Footer at bottom (after the horizontal separator)
    move_cursor(height, 1);
    set_color(32); // Green for footer text
    printf("Ctrl+C to escape");
    reset_colors();

    fflush(stdout);
}
