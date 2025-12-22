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
void update_pane_scroll(pane_scroll_state_t* scroll_state, int direction, int amount) {
    if (!scroll_state || amount <= 0) return;

    // CRITICAL FIX: Validate max_scroll is valid
    if (scroll_state->max_scroll < 0) {
        scroll_state->max_scroll = 0;
    }

    // Clamp current position before updating
    if (scroll_state->scroll_position < 0) {
        scroll_state->scroll_position = 0;
    }
    if (scroll_state->scroll_position > scroll_state->max_scroll) {
        scroll_state->scroll_position = scroll_state->max_scroll;
    }

    int delta = direction * amount;

    // Update scroll position
    scroll_state->scroll_position += delta;

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

// Extract repository name from a "Repository: reponame" header string
static char* extract_repo_name_from_header(const char* item) {
    if (!item) return NULL;

    const char* prefix = "Repository: ";
    size_t prefix_len = strlen(prefix);

    if (strncmp(item, prefix, prefix_len) == 0) {
        return strdup(item + prefix_len);
    }

    return NULL;
}

// Draw a single pane with scroll support (pane 3 uses animations instead of items)
void draw_pane(int start_col, int width, int height, const char* title, char** items, size_t item_count, int title_color, const style_config_t* styles, int pane_index, const pane_scroll_state_t* scroll_state, three_pane_tui_orchestrator_t* orch) {
    // Safety checks
    if (!title || !styles || width <= 0 || height <= 0) {
        return;
    }

    // Handle pane 3 (right pane) - render animations instead of items
    if (pane_index == 3) {
        // Debug: Clear the entire pane first
        for (int row = 3; row <= 3 + height; row++) {
            move_cursor(row, start_col);
            for (int col = 0; col < width; col++) {
                putchar(' ');
            }
        }

        // Draw title
        move_cursor(3, start_col);
        set_color(title_color);
        set_bold();
        printf("%s", title);
        reset_colors();

        // Render active animations starting from row 4
        int current_row = 4;
        int max_row = 3 + height;
        int last_animation_row = current_row;

        for (size_t i = 0; i < orch->data.active_animation_count && current_row <= max_row; i++) {
            animation_state_t* anim = orch->data.active_animations[i];
            if (anim) {
                render_scroll_left_right(anim, current_row, start_col, width);
                last_animation_row = current_row + 1;
                current_row++;
            }
        }

        // Clear any remaining rows in the pane that no longer have animations
        for (int clear_row = last_animation_row; clear_row <= max_row; clear_row++) {
            move_cursor(clear_row, start_col);
            // Clear the entire row by printing spaces
            for (int col = 0; col < width; col++) {
                putchar(' ');
            }
        }

        return; // Done rendering pane 3
    }

    // For panes 1 and 2, use the original items-based rendering
    if (!items) {
        return;
    }

    // Draw title at the top of the pane (row 3, since row 1 is main title, row 2 is header separator)
    if (pane_index == 2) {
        // Center the center pane title
        int title_len = strlen(title);
        int center_col = start_col + (width - title_len) / 2;
        // Ensure it doesn't go beyond the pane boundaries
        if (center_col < start_col) center_col = start_col;
        if (center_col + title_len > start_col + width) center_col = start_col + width - title_len;
        move_cursor(3, center_col);
    } else {
        move_cursor(3, start_col);
    }
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

    // Phase 1: Assign alternating colors to ALL items (before calculating visible range)
    int* item_colors = calloc(item_count, sizeof(int));
    int current_repo_color = 0; // Will be assigned alternating colors 1, 2, 3, 4, etc.

    for (size_t i = 0; i < item_count; i++) {
        char* repo_name = extract_repo_name_from_header(items[i]);
        if (repo_name) {
            // Repository header - assign next alternating color
            current_repo_color++;
            // Wrap around to rainbow table (1-8)
            if (current_repo_color > 8) current_repo_color = 1;
            item_colors[i] = current_repo_color;
            free(repo_name);
        } else {
            // Content item - use the current repository's color
            item_colors[i] = current_repo_color;
        }
    }

    // Calculate which items to show based on scroll position
    size_t start_item = scroll_state ? scroll_state->scroll_position : 0;

    // CRITICAL FIX: Validate start_item against item_count
    if (start_item >= item_count) {
        start_item = (item_count > 0) ? item_count - 1 : 0;
    }

    size_t end_item = start_item + height;
    if (end_item > item_count) {
        end_item = item_count;
    }

    // Additional safety: ensure start_item doesn't exceed end_item
    if (start_item > end_item) {
        start_item = (end_item > 0) ? end_item - 1 : 0;
    }

    // Draw visible items only
    for (size_t i = start_item; i < end_item && current_row <= max_row; i++) {
        // CRITICAL FIX: Validate array access
        if (i >= item_count || !items[i]) {
            break; // Safety: stop if out of bounds
        }
        move_cursor(current_row, start_col);

        // Check if this is a repository header
        char* repo_name = extract_repo_name_from_header(items[i]);
        if (repo_name) {
            // This is a repository header - center it and use adjusted repo color
            int repo_ansi_color = color_index_to_ansi(item_colors[i]);

            // Center the header text within the pane width
            int text_len = strlen(items[i]);
            int center_col = start_col + (width - text_len) / 2;
            if (center_col < start_col) center_col = start_col;
            if (center_col + text_len > start_col + width) center_col = start_col + width - text_len;

            move_cursor(current_row, center_col);
            set_color(repo_ansi_color);
            set_bold();
            printf("%s", items[i]);
            reset_colors();

            free(repo_name);
        } else {
            // This is a content item - use adjusted repo color or file color
            int item_color = item_colors[i] ? color_index_to_ansi(item_colors[i]) : get_file_color(items[i], styles);
            set_color(item_color);

            // Smart truncation prioritizing filename over directory path
            const char* text = items[i];
            int max_text_width = width; // Fill the pane completely

            // Use glyph-aware right-priority truncation for all content
            char* display_text = truncate_string_right_priority(text, max_text_width);
            if (display_text) {
                printf("%s", display_text);
                free(display_text);
            } else {
                printf("(null)");
            }
            reset_colors();
        }

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

    // Cleanup color array
    free(item_colors);
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
              orch->data.pane1_items, orch->data.pane1_count, orch->config.styles.ui.pane_titles.left, &orch->config.styles, 1, &orch->data.pane1_scroll, orch);

    draw_pane(pane_width + 1, pane_width - 1, pane_height, orch->config.pane2_title,
              orch->data.pane2_items, orch->data.pane2_count, orch->config.styles.ui.pane_titles.center, &orch->config.styles, 2, &orch->data.pane2_scroll, orch);

    // Rightmost pane gets any remaining width minus the border (uses animations, not items)
    draw_pane(pane_width * 2 + 1, pane_width + remaining_width - 1, pane_height, orch->config.pane3_title,
              NULL, 0, orch->config.styles.ui.pane_titles.right, &orch->config.styles, 3, NULL, orch);

    // Show fast scroll progress bar in pane 1 if active (overlay on content)
    if (is_scroll_animation_active(orch)) {
        // Calculate animation progress
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - orch->data.scroll_animation.start_time.tv_sec) +
                        (now.tv_nsec - orch->data.scroll_animation.start_time.tv_nsec) / 1e9;
        double progress = elapsed / orch->data.scroll_animation.duration_sec;
        if (progress > 1.0) progress = 1.0;

        // Display progress bar in the bottom of pane 1 (overlay on content)
        int progress_row = 3 + pane_height - 1; // Bottom row of pane 1
        int progress_col = 1; // Start of pane 1

        move_cursor(progress_row, progress_col);
        set_color(32); // Green for progress bar
        set_bold();
        printf("FAST SCROLL [");
        int bar_width = pane_width - 15; // Leave space for text
        if (bar_width > 20) bar_width = 20; // Cap at reasonable width
        int filled = (int)(progress * bar_width);
        for (int i = 0; i < bar_width; i++) {
            if (i < filled) printf("█");
            else printf("░");
        }
        printf("] %.0f%%", progress * 100.0);
        reset_colors();
    }

    // Footer at bottom (after the horizontal separator)
    move_cursor(height, 1);
    set_color(32); // Green for footer text
    const char* current_view = (orch->current_view == VIEW_FLAT) ? "FLAT" : "TREE";
    printf("Ctrl+C to escape | [%s] click to toggle view", current_view);
    reset_colors();

    fflush(stdout);
}
