#include "../three-pane-tui.h"
#include <locale.h>
#include <unistd.h>

// Global flag for redraw requests
volatile sig_atomic_t redraw_needed = 0;

// Load configuration from index.json
int load_config(three_pane_tui_orchestrator_t* orch) {
    // Load JSON config
    json_value_t* config = json_parse_file("index.json");
    if (!config || config->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load config\n");
        return -1;
    }

    // Extract config values with defaults
    orch->config.title = expandvars("Three Pane TUI Demo");
    orch->config.exit_keys = strdup("qQ");
    orch->config.toggle_keys = strdup(" ");
    orch->config.pane1_title = expandvars("Left Pane");
    orch->config.pane2_title = expandvars("Center Pane");
    orch->config.pane3_title = expandvars("Right Pane");
    orch->config.default_view = VIEW_FLAT;
    orch->current_view = orch->config.default_view;

    // Load styles
    if (load_styles(&orch->config.styles, orch->module_path) != 0) {
        fprintf(stderr, "Failed to load styles\n");
        json_free(config);
        return -1;
    }

    json_free(config);
    return 0;
}

// Initialize orchestrator
three_pane_tui_orchestrator_t* three_pane_tui_init(const char* module_path) {
    // Set locale for Unicode support (required for glyph width calculations)
    setlocale(LC_ALL, "");

    three_pane_tui_orchestrator_t* orch = calloc(1, sizeof(three_pane_tui_orchestrator_t));
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

    // Load hardcoded data for pane 1 (left pane) - add more items to test scrolling
    orch->data.pane1_count = 15;
    orch->data.pane1_items = calloc(15, sizeof(char*));
    orch->data.pane1_items[0] = strdup("n00dles");
    orch->data.pane1_items[1] = strdup("├── src/");
    orch->data.pane1_items[2] = strdup("├── main.c");
    orch->data.pane1_items[3] = strdup("├── ui.c");
    orch->data.pane1_items[4] = strdup("├── core.c");
    orch->data.pane1_items[5] = strdup("├── data.c");
    orch->data.pane1_items[6] = strdup("├── styles.c");
    orch->data.pane1_items[7] = strdup("├── three-pane-tui.h");
    orch->data.pane1_items[8] = strdup("├── Makefile");
    orch->data.pane1_items[9] = strdup("├── index.json");
    orch->data.pane1_items[10] = strdup("├── README.md");
    orch->data.pane1_items[11] = strdup("├── LICENSE");
    orch->data.pane1_items[12] = strdup("└── .gitignore");
    orch->data.pane1_items[13] = strdup("repoWatch/");
    orch->data.pane1_items[14] = strdup("serverGenesis/");

    // Initialize scroll states
    orch->data.pane1_scroll.scroll_position = 0;
    orch->data.pane1_scroll.total_items = orch->data.pane1_count;
    orch->data.pane2_scroll.scroll_position = 0;
    orch->data.pane2_scroll.total_items = orch->data.pane2_count;
    orch->data.pane3_scroll.scroll_position = 0;
    orch->data.pane3_scroll.total_items = orch->data.pane3_count;

    if (load_committed_not_pushed_data(orch, orch->current_view) != 0) {
        fprintf(stderr, "Warning: Failed to load committed-not-pushed data, using fallback\n");
        // Could add fallback data here if needed
    }

    if (load_hardcoded_data(orch) != 0) {
        free(orch->config.title);
        free(orch->config.exit_keys);
        free(orch->config.pane1_title);
        free(orch->config.pane2_title);
        free(orch->config.pane3_title);
        free(orch->module_path);
        free(orch);
        return NULL;
    }

    return orch;
}

// Cleanup orchestrator
void three_pane_tui_cleanup(three_pane_tui_orchestrator_t* orch) {
    if (orch) {
        // Cleanup config
        free(orch->config.title);
        free(orch->config.exit_keys);
        free(orch->config.toggle_keys);
        free(orch->config.pane1_title);
        free(orch->config.pane2_title);
        free(orch->config.pane3_title);

        // Cleanup data
        for (size_t i = 0; i < orch->data.pane1_count; i++) {
            free(orch->data.pane1_items[i]);
        }
        free(orch->data.pane1_items);

        for (size_t i = 0; i < orch->data.pane2_count; i++) {
            free(orch->data.pane2_items[i]);
        }
        free(orch->data.pane2_items);

        for (size_t i = 0; i < orch->data.pane3_count; i++) {
            free(orch->data.pane3_items[i]);
        }
        free(orch->data.pane3_items);

        free(orch->module_path);
        free(orch);
    }
}

// Execute the three-pane-tui module
int three_pane_tui_execute(three_pane_tui_orchestrator_t* orch) {
    struct timespec last_redraw;
    struct timespec last_button_click;
    clock_gettime(CLOCK_MONOTONIC, &last_redraw);
    clock_gettime(CLOCK_MONOTONIC, &last_button_click);

    // Set up signal handlers
    struct sigaction sa;

    // Window resize handler
    sa.sa_handler = handle_sigwinch;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    // Emergency cleanup handlers for crash signals
    sa.sa_handler = emergency_cleanup;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);  // Segmentation fault
    sigaction(SIGABRT, &sa, NULL);  // Abort
    sigaction(SIGBUS, &sa, NULL);   // Bus error
    sigaction(SIGILL, &sa, NULL);   // Illegal instruction
    sigaction(SIGFPE, &sa, NULL);   // Floating point exception

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

    // Enable mouse reporting
    if (enable_mouse_reporting() != 0) {
        fprintf(stderr, "Warning: Failed to enable mouse reporting\n");
    }

    // Hide cursor and save position
    hide_cursor();
    save_cursor_position();

    // Draw the initial TUI overlay
    draw_tui_overlay(orch);

    // Main input loop
    int running = 1;
    int width, height;
    get_terminal_size(&width, &height);
    int pane_width = width / 3;
    int pane_height = height - 5; // Available rows for content

    // Update initial scroll states
    update_scroll_state(&orch->data.pane1_scroll, pane_height, orch->data.pane1_count);
    update_scroll_state(&orch->data.pane2_scroll, pane_height, orch->data.pane2_count);
    update_scroll_state(&orch->data.pane3_scroll, pane_height, orch->data.pane3_count);

    while (running) {
        // Check for redraw request from signal handler
        if (redraw_needed) {
            redraw_needed = 0;
            // Recalculate dimensions on resize
            get_terminal_size(&width, &height);
            pane_width = width / 3;
            pane_height = height - 5;
            // Update scroll states for new dimensions
            update_scroll_state(&orch->data.pane1_scroll, pane_height, orch->data.pane1_count);
            update_scroll_state(&orch->data.pane2_scroll, pane_height, orch->data.pane2_count);
            update_scroll_state(&orch->data.pane3_scroll, pane_height, orch->data.pane3_count);
            draw_tui_overlay(orch);
        }

        // Try to read mouse events first (they start with \033)
        int button, x, y, scroll_delta;
        int mouse_result = read_mouse_event(&button, &x, &y, &scroll_delta);

        if (mouse_result == 0) {
            // Only handle button presses, not releases (button == -1 indicates release)
            if (button == -1) {
                // Button release - ignore to prevent double-clicking
                // Do nothing
            } else {
                // Button press - handle it
                // Convert to 1-based coordinates
                int click_x = x - 1;
                int click_y = y - 1;

                // Check if click is in footer (last row)
                if (click_y == height - 1) {
                    // Check if click is on the toggle button (around columns 10-20)
                    if (click_x >= 10 && click_x <= 20) {
                        // Check cooldown (1 second minimum between clicks)
                        struct timespec now;
                        clock_gettime(CLOCK_MONOTONIC, &now);
                        long elapsed_ms = (now.tv_sec - last_button_click.tv_sec) * 1000 +
                                        (now.tv_nsec - last_button_click.tv_nsec) / 1000000;

                        if (elapsed_ms >= 1000) {
                            // Toggle view mode
                            last_button_click = now;

                            orch->current_view = (orch->current_view == VIEW_FLAT) ? VIEW_TREE : VIEW_FLAT;

                            // Reload data with new view mode
                            if (load_committed_not_pushed_data(orch, orch->current_view) == 0) {
                                draw_tui_overlay(orch);
                            }
                        }
                    }
                } else {
                // Handle pane scrolling (not in footer)
                int pane_index = get_pane_at_position(click_x, click_y, pane_width, width, pane_height);
                if (pane_index > 0 && pane_index <= 3) {
                    pane_scroll_state_t* scroll_state = NULL;
                    switch (pane_index) {
                        case 1: scroll_state = &orch->data.pane1_scroll; break;
                        case 2: scroll_state = &orch->data.pane2_scroll; break;
                        case 3: scroll_state = &orch->data.pane3_scroll; break;
                    }
                    if (scroll_state) {
                        update_pane_scroll(scroll_state, scroll_delta);

                        // Throttle redraws to prevent crashes from rapid mouse events
                        struct timespec now;
                        clock_gettime(CLOCK_MONOTONIC, &now);
                        long elapsed_ms = (now.tv_sec - last_redraw.tv_sec) * 1000 +
                                        (now.tv_nsec - last_redraw.tv_nsec) / 1000000;

                        if (elapsed_ms >= 50) { // Minimum 50ms between redraws
                            draw_tui_overlay(orch);
                            last_redraw = now;
                        }
                    }
                }
            }
            }
        } else if (mouse_result == -3) {
            // Incomplete mouse event, ignore and continue
            // This prevents crashes from partial mouse event data
        } else {
            // No mouse event or invalid data, check for keyboard input
            int c = read_char_timeout();
            if (c >= 0) {
                // Check for exit keys
                if (c == 'q' || c == 'Q' || c == 27) { // 27 is Escape
                    running = 0;
                }
            }
        }

        // Small delay to prevent busy waiting
        struct timespec delay = {0, 10000000}; // 10ms
        nanosleep(&delay, NULL);
    }

    // Cleanup: restore terminal state
    clear_screen();
    restore_cursor_position();
    show_cursor();

    // Disable mouse reporting
    disable_mouse_reporting();

    // Restore blocking mode
    fcntl(STDIN_FILENO, F_SETFL, flags);

    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

    return 0;
}
