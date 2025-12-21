#include "../three-pane-tui.h"

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
    orch->config.pane1_title = expandvars("Left Pane");
    orch->config.pane2_title = expandvars("Center Pane");
    orch->config.pane3_title = expandvars("Right Pane");

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

    // Load hardcoded data for pane 1 (left pane)
    orch->data.pane1_count = 1;
    orch->data.pane1_items = calloc(1, sizeof(char*));
    orch->data.pane1_items[0] = strdup("n00dles");

    if (load_committed_not_pushed_data(orch) != 0) {
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
