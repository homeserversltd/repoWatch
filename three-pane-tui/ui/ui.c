#include "../three-pane-tui.h"

// Draw a single pane
void draw_pane(int start_col, int width, int height, const char* title, char** items, size_t item_count, int title_color, const style_config_t* styles, int pane_index) {
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
    for (size_t i = 0; i < item_count && current_row <= max_row; i++) {
        move_cursor(current_row, start_col);
        // Apply color based on file type
        int file_color = get_file_color(items[i], styles);
        set_color(file_color);

        // Smart truncation prioritizing filename over directory path
        const char* text = items[i];
        int max_text_width = width - 2; // Leave some margin

        if (strlen(text) > max_text_width) {
            char truncated[1024];

            // For file entries (containing "├── "), prioritize showing the filename
            if (strstr(text, "├── ")) {
                const char* filename_start = strstr(text, "├── ");
                if (filename_start) {
                    filename_start += 4; // Skip "├── "
                    const char* prefix_end = filename_start;
                    int prefix_len = prefix_end - text;

                    // Preserve the exact tree prefix
                    char preserved_prefix[32];
                    snprintf(preserved_prefix, sizeof(preserved_prefix), "%.*s", prefix_len, text);

                    int available_for_filename = max_text_width - prefix_len - 3; // 3 for "..."

                    if (available_for_filename > 0) {
                        int filename_len = strlen(filename_start);
                        if (filename_len <= available_for_filename) {
                            // Can show full filename
                            snprintf(truncated, sizeof(truncated), "%s%s", preserved_prefix, filename_start);
                        } else {
                            // Show ellipsis + end of filename
                            int show_from = filename_len - available_for_filename;
                            if (show_from > 0) {
                                snprintf(truncated, sizeof(truncated), "%s...%s", preserved_prefix, filename_start + show_from);
                            } else {
                                // Fallback: just truncate normally
                                snprintf(truncated, sizeof(truncated), "%.*s...", max_text_width - 3, text);
                            }
                        }
                    } else {
                        // Not enough space, truncate normally
                        snprintf(truncated, sizeof(truncated), "%.*s...", max_text_width - 3, text);
                    }
                } else {
                    // Fallback for malformed entries
                    snprintf(truncated, sizeof(truncated), "%.*s...", max_text_width - 3, text);
                }
            } else {
                // For non-file entries (like repository names), truncate from end
                int text_len = strlen(text);
                int start_pos = text_len - (max_text_width - 3); // Leave room for "..."
                if (start_pos > 0) {
                    snprintf(truncated, sizeof(truncated), "...%s", text + start_pos);
                } else {
                    // Fallback: just truncate normally
                    snprintf(truncated, sizeof(truncated), "%.*s...", max_text_width - 3, text);
                }
            }
            printf("%s", truncated);
        } else {
            printf("%s", text);
        }
        reset_colors();
        current_row++;
    }
}

// Draw the three-pane TUI overlay
void draw_tui_overlay(three_pane_tui_orchestrator_t* orch) {
    int width, height;
    get_terminal_size(&width, &height);

    clear_screen();

    // Main title at the very top
    move_cursor(1, 1);
    set_color(orch->config.styles.ui.title_color);
    set_bold();
    printf("%s", orch->config.title);
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
    int pane_height = height - 4; // Available rows: total height minus main title row, header separator, pane title row, and footer separator

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
              orch->data.pane1_items, orch->data.pane1_count, orch->config.styles.ui.pane_titles.left, &orch->config.styles, 1);

    draw_pane(pane_width + 1, pane_width - 1, pane_height, orch->config.pane2_title,
              orch->data.pane2_items, orch->data.pane2_count, orch->config.styles.ui.pane_titles.center, &orch->config.styles, 2);

    // Rightmost pane gets any remaining width minus the border
    draw_pane(pane_width * 2 + 1, pane_width + remaining_width - 1, pane_height, orch->config.pane3_title,
              orch->data.pane3_items, orch->data.pane3_count, orch->config.styles.ui.pane_titles.right, &orch->config.styles, 3);

    // Footer at bottom (after the horizontal separator)
    move_cursor(height, 1);
    set_color(32); // Green for footer text
    printf("Q: exit - Three Pane TUI Demo");
    reset_colors();

    fflush(stdout);
}
