#include "../three-pane-tui.h"
#include <locale.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>

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
    orch->config.pane1_title = strdup("Dirty files");
    orch->config.pane2_title = strdup("Committed Files");
    orch->config.pane3_title = strdup("Active files");
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

// Check if a file was present at startup
int was_startup_file(three_pane_tui_orchestrator_t* orch, const char* filepath) {
    if (!orch || !filepath) return 0;

    for (size_t i = 0; i < orch->data.startup_file_count; i++) {
        if (strcmp(orch->data.startup_files[i], filepath) == 0) {
            return 1;
        }
    }
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

    // Load dirty files data for pane 1 (left pane)
    if (load_dirty_files_data(orch, orch->current_view) != 0) {
        fprintf(stderr, "Warning: Failed to load dirty files data, using empty pane\n");
    }

    // Initialize pane1 and pane2 scroll state (after data is loaded)
    orch->data.pane1_scroll.scroll_position = 0;
    orch->data.pane1_scroll.total_items = orch->data.pane1_count;
    orch->data.pane2_scroll.scroll_position = 0;
    orch->data.pane2_scroll.total_items = orch->data.pane2_count;
    // pane3 uses animations, not scroll state

    // Initialize scroll animation state
    memset(&orch->data.scroll_animation, 0, sizeof(scroll_animation_t));

    if (load_committed_not_pushed_data(orch, orch->current_view) != 0) {
        fprintf(stderr, "Warning: Failed to load committed-not-pushed data, using fallback\n");
        // Could add fallback data here if needed
    }

    // Capture files that are currently dirty at startup (don't animate these)
    size_t startup_count = 0;
    active_file_info_t* startup_files = load_file_changes_data(&startup_count);

    if (startup_files && startup_count > 0) {
        orch->data.startup_files = calloc(startup_count, sizeof(char*));
        if (orch->data.startup_files) {
            orch->data.startup_file_count = startup_count;
            for (size_t i = 0; i < startup_count; i++) {
                orch->data.startup_files[i] = strdup(startup_files[i].path);
            }
        }

        // Cleanup temporary array
        for (size_t i = 0; i < startup_count; i++) {
            free(startup_files[i].path);
        }
        free(startup_files);
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

        // Cleanup active animations (replaces pane3_items)
        for (size_t i = 0; i < orch->data.active_animation_count; i++) {
            cleanup_animation_state(orch->data.active_animations[i]);
        }
        free(orch->data.active_animations);

        // Cleanup startup files
        for (size_t i = 0; i < orch->data.startup_file_count; i++) {
            free(orch->data.startup_files[i]);
        }
        free(orch->data.startup_files);

        free(orch->module_path);
        free(orch);
    }
}

// Scroll detection helper functions
#define SCROLL_HISTORY_SIZE 10

static struct timespec scroll_timestamps[SCROLL_HISTORY_SIZE];
static int scroll_directions[SCROLL_HISTORY_SIZE];
static int scroll_history_count = 0;
static int scroll_history_index = 0;

double calculate_scroll_frequency(void) {
    if (scroll_history_count < 2) return 0.0;

    // Calculate time span of recent scroll events
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    struct timespec earliest = scroll_timestamps[(scroll_history_index - scroll_history_count + SCROLL_HISTORY_SIZE) % SCROLL_HISTORY_SIZE];
    double time_span = (now.tv_sec - earliest.tv_sec) + (now.tv_nsec - earliest.tv_nsec) / 1e9;

    if (time_span <= 0.0) return 0.0;

    return scroll_history_count / time_span; // events per second
}

int count_consecutive_scrolls(void) {
    if (scroll_history_count < 2) return 0;

    // Count consecutive scrolls in same direction
    int consecutive = 1;
    int last_direction = scroll_directions[(scroll_history_index - 1 + SCROLL_HISTORY_SIZE) % SCROLL_HISTORY_SIZE];

    for (int i = 2; i <= scroll_history_count && i <= SCROLL_HISTORY_SIZE; i++) {
        int idx = (scroll_history_index - i + SCROLL_HISTORY_SIZE) % SCROLL_HISTORY_SIZE;
        if (scroll_directions[idx] == last_direction) {
            consecutive++;
        } else {
            break;
        }
    }

    return consecutive;
}

double calculate_scroll_acceleration(void) {
    if (scroll_history_count < 3) return 0.0;

    // Calculate acceleration as change in scroll frequency
    // Simplified: number of direction changes in recent history
    int direction_changes = 0;
    int last_direction = scroll_directions[(scroll_history_index - 1 + SCROLL_HISTORY_SIZE) % SCROLL_HISTORY_SIZE];

    for (int i = 2; i <= scroll_history_count && i <= SCROLL_HISTORY_SIZE; i++) {
        int idx = (scroll_history_index - i + SCROLL_HISTORY_SIZE) % SCROLL_HISTORY_SIZE;
        if (scroll_directions[idx] != last_direction) {
            direction_changes++;
            last_direction = scroll_directions[idx];
        }
    }

    // Lower direction changes = higher acceleration (consistent direction)
    return (double)(scroll_history_count - direction_changes) / scroll_history_count;
}

void record_scroll_event(int direction) {
    // Record timestamp and direction
    clock_gettime(CLOCK_MONOTONIC, &scroll_timestamps[scroll_history_index]);
    scroll_directions[scroll_history_index] = direction;

    scroll_history_index = (scroll_history_index + 1) % SCROLL_HISTORY_SIZE;
    if (scroll_history_count < SCROLL_HISTORY_SIZE) {
        scroll_history_count++;
    }
}

bool is_fast_scrolling_detected(void) {
    double events_per_second = calculate_scroll_frequency();
    int consecutive_events = count_consecutive_scrolls();
    double acceleration = calculate_scroll_acceleration();

    // More conservative thresholds for fast scrolling detection
    bool is_fast = (events_per_second > 8.0) ||  // Need 8+ events per second
                   (consecutive_events >= 5) ||  // Need 5+ consecutive scrolls
                   (acceleration > 2.5);         // Need high acceleration


    return is_fast;
}

bool is_at_scroll_boundary(pane_scroll_state_t* scroll_state, int direction) {
    if (!scroll_state) return true;

    if (direction > 0) { // Scrolling down
        return scroll_state->scroll_position >= scroll_state->max_scroll;
    } else if (direction < 0) { // Scrolling up
        return scroll_state->scroll_position <= 0;
    }

    return false;
}

// Scroll animation functions for smooth transitions
void start_scroll_animation(three_pane_tui_orchestrator_t* orch, int pane_index, int target_position) {
    if (!orch) return;

    // Get current scroll position
    pane_scroll_state_t* scroll_state = NULL;
    switch (pane_index) {
        case 1: scroll_state = &orch->data.pane1_scroll; break;
        case 2: scroll_state = &orch->data.pane2_scroll; break;
        default: return; // Pane 3 doesn't use scroll state
    }
    if (!scroll_state) return;

    // Clamp target position to valid range
    if (target_position < 0) target_position = 0;
    if (target_position > scroll_state->max_scroll) target_position = scroll_state->max_scroll;

    // Start animation
    orch->data.scroll_animation.is_animating = 1;
    orch->data.scroll_animation.start_position = scroll_state->scroll_position;
    orch->data.scroll_animation.target_position = target_position;
    orch->data.scroll_animation.pane_index = pane_index;
    orch->data.scroll_animation.duration_sec = 0.15; // 150ms animation for snappy feel
    clock_gettime(CLOCK_MONOTONIC, &orch->data.scroll_animation.start_time);
}

void update_scroll_animation(three_pane_tui_orchestrator_t* orch) {
    if (!orch || !orch->data.scroll_animation.is_animating) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    // Calculate elapsed time
    double elapsed = (now.tv_sec - orch->data.scroll_animation.start_time.tv_sec) +
                    (now.tv_nsec - orch->data.scroll_animation.start_time.tv_nsec) / 1e9;

    // Check if animation is complete
    if (elapsed >= orch->data.scroll_animation.duration_sec) {
        // Animation complete - set final position
        pane_scroll_state_t* scroll_state = NULL;
        switch (orch->data.scroll_animation.pane_index) {
            case 1: scroll_state = &orch->data.pane1_scroll; break;
            case 2: scroll_state = &orch->data.pane2_scroll; break;
        }
        if (scroll_state) {
            scroll_state->scroll_position = orch->data.scroll_animation.target_position;
        }
        orch->data.scroll_animation.is_animating = 0;
        return;
    }

    // Ease-out animation (starts fast, slows down)
    double progress = elapsed / orch->data.scroll_animation.duration_sec;
    double eased_progress = 1.0 - pow(1.0 - progress, 3.0); // Cubic ease-out

    // Calculate current position
    int start_pos = orch->data.scroll_animation.start_position;
    int target_pos = orch->data.scroll_animation.target_position;
    int current_pos = start_pos + (int)((target_pos - start_pos) * eased_progress);

    // Update scroll position
    pane_scroll_state_t* scroll_state = NULL;
    switch (orch->data.scroll_animation.pane_index) {
        case 1: scroll_state = &orch->data.pane1_scroll; break;
        case 2: scroll_state = &orch->data.pane2_scroll; break;
    }
    if (scroll_state) {
        scroll_state->scroll_position = current_pos;
        // Ensure bounds
        if (scroll_state->scroll_position < 0) scroll_state->scroll_position = 0;
        if (scroll_state->scroll_position > scroll_state->max_scroll) scroll_state->scroll_position = scroll_state->max_scroll;
    }
}

int is_scroll_animation_active(three_pane_tui_orchestrator_t* orch) {
    return orch && orch->data.scroll_animation.is_animating;
}

void cancel_scroll_animation(three_pane_tui_orchestrator_t* orch) {
    if (orch) {
        orch->data.scroll_animation.is_animating = 0;
    }
}

// Execute the three-pane-tui module
int three_pane_tui_execute(three_pane_tui_orchestrator_t* orch) {
    // TEMPORARILY DISABLE TTY CHECK TO SEE ACTUAL CRASH
    // Check if we have a TTY - exit early if not (prevents blocking on terminal operations)
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "Error: three-pane-tui requires a TTY for interactive operation\n");
        return 2; // Exit code 2 to indicate TTY requirement
    }

    struct timespec last_redraw;
    struct timespec last_button_click;
    struct timespec last_git_check;
    clock_gettime(CLOCK_MONOTONIC, &last_redraw);
    clock_gettime(CLOCK_MONOTONIC, &last_button_click);
    clock_gettime(CLOCK_MONOTONIC, &last_git_check);

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
    struct timespec initial_draw_start, initial_draw_end;
    clock_gettime(CLOCK_MONOTONIC, &initial_draw_start);
    draw_tui_overlay(orch);
    clock_gettime(CLOCK_MONOTONIC, &initial_draw_end);

    double initial_draw_time = (initial_draw_end.tv_sec - initial_draw_start.tv_sec) +
                              (initial_draw_end.tv_nsec - initial_draw_start.tv_nsec) / 1e9;
    fprintf(stderr, "PERF: INITIAL DRAW: %.3f seconds\n", initial_draw_time);

    // Main input loop
    int running = 1;
    int width, height;
    get_terminal_size(&width, &height);

    // Minimum size check to prevent crashes
    if (width < 20 || height < 10) {
        printf("Terminal too small. Minimum size: 20x10\n");
        return 1;
    }

    int pane_width = width / 3;
    // Ensure minimum pane width to prevent negative values
    if (pane_width < 1) pane_width = 1;

    int pane_height = height - 5; // Available rows for content

    // Update initial scroll states (pane3 uses animations, not scroll state)
    update_scroll_state(&orch->data.pane1_scroll, pane_height, orch->data.pane1_count);
    update_scroll_state(&orch->data.pane2_scroll, pane_height, orch->data.pane2_count);

    int iteration_count = 0;
    struct timespec loop_start_time, last_log_time;
    clock_gettime(CLOCK_MONOTONIC, &loop_start_time);
    clock_gettime(CLOCK_MONOTONIC, &last_log_time);

    while (running) {
        iteration_count++;
        struct timespec iteration_start;
        clock_gettime(CLOCK_MONOTONIC, &iteration_start);

        // Debug output for first few iterations
        if (iteration_count <= 3) {
            fprintf(stderr, "DEBUG: Main loop iteration %d starting\n", iteration_count);
        }

        // Periodic debug output (every 1000 iterations)
        if (iteration_count % 1000 == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double time_since_start = (now.tv_sec - loop_start_time.tv_sec) +
                                     (now.tv_nsec - loop_start_time.tv_nsec) / 1e9;
            double time_since_last_log = (now.tv_sec - last_log_time.tv_sec) +
                                        (now.tv_nsec - last_log_time.tv_nsec) / 1e9;

            fprintf(stderr, "PERF: Iteration %d (%.2fs total, %.2fs since last log), animations: %zu, width: %d, height: %d\n",
                   iteration_count, time_since_start, time_since_last_log,
                   orch->data.active_animation_count, width, height);

            clock_gettime(CLOCK_MONOTONIC, &last_log_time);
        }

        // Check for redraw request from signal handler
        if (redraw_needed) {
            redraw_needed = 0;
            // Recalculate dimensions on resize
            get_terminal_size(&width, &height);

            // Minimum size check
            if (width >= 20 && height >= 10) {
                pane_width = width / 3;
                if (pane_width < 1) pane_width = 1;
                pane_height = height - 5;
                // Update scroll states for new dimensions (pane3 uses animations, not scroll state)
                update_scroll_state(&orch->data.pane1_scroll, pane_height, orch->data.pane1_count);
                update_scroll_state(&orch->data.pane2_scroll, pane_height, orch->data.pane2_count);
            }

            draw_tui_overlay(orch);
        }

        // Check if 200ms have elapsed since last git data refresh
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - last_git_check.tv_sec) * 1000 +
                         (now.tv_nsec - last_git_check.tv_nsec) / 1000000;

        if (elapsed_ms >= 200) {  // 200ms refresh interval
            // Refresh git data by re-running all components
            int dirty_files_result = system("./dirty-files/dirty-files > /dev/null 2>&1");
            int committed_not_pushed_result = system("./committed-not-pushed/committed-not-pushed > /dev/null 2>&1");
            system("./file-changes-watcher/file-changes-watcher > /dev/null 2>&1"); // Always run, no result check needed

            // Reload data for each pane that succeeded (always attempt all)
            int data_changed = 0;
            if (dirty_files_result == 0 && load_dirty_files_data(orch, orch->current_view) == 0) {
                data_changed = 1;
            }
            if (committed_not_pushed_result == 0 && load_committed_not_pushed_data(orch, orch->current_view) == 0) {
                data_changed = 1;
            }
            // Note: file-changes-watcher data is loaded below in animation management, no separate load function needed

            // Update UI if any data changed
            if (data_changed) {
                // Update scroll states after data refresh
                get_terminal_size(&width, &height);
                if (width >= 20 && height >= 10) {
                    pane_width = width / 3;
                    if (pane_width < 1) pane_width = 1;
                    pane_height = height - 5;
                    update_scroll_state(&orch->data.pane1_scroll, pane_height, orch->data.pane1_count);
                    update_scroll_state(&orch->data.pane2_scroll, pane_height, orch->data.pane2_count);

                    // CRITICAL FIX: Clamp scroll positions after data refresh
                    if (orch->data.pane1_scroll.scroll_position > orch->data.pane1_scroll.max_scroll) {
                        orch->data.pane1_scroll.scroll_position = orch->data.pane1_scroll.max_scroll;
                    }
                    if (orch->data.pane2_scroll.scroll_position > orch->data.pane2_scroll.max_scroll) {
                        orch->data.pane2_scroll.scroll_position = orch->data.pane2_scroll.max_scroll;
                    }

                    // pane3 uses animations, not scroll state
                }
                draw_tui_overlay(orch);
            }

            // Manage animation states for active file changes
            size_t active_file_count = 0;
            active_file_info_t* active_files = load_file_changes_data(&active_file_count);

            if (active_files) {
                time_t now = time(NULL);

                // Remove expired animations (safely handle rapid updates)
                size_t write_idx = 0;
                for (size_t i = 0; i < orch->data.active_animation_count && i < 1000; i++) { // Safety limit
                    animation_state_t* anim = orch->data.active_animations[i];
                    if (anim && !is_animation_expired(anim, now)) {
                        // Keep this animation
                        if (write_idx != i) {
                            orch->data.active_animations[write_idx] = orch->data.active_animations[i];
                        }
                        write_idx++;
                    } else if (anim) {
                        // Remove expired animation
                        cleanup_animation_state(anim);
                    }
                }
                orch->data.active_animation_count = write_idx;

                // Update existing animations and add new ones
                for (size_t i = 0; i < active_file_count; i++) {
                    active_file_info_t* file_info = &active_files[i];
                    int found = 0;

                    // Check if we already have an animation for this file
                    for (size_t j = 0; j < orch->data.active_animation_count; j++) {
                        animation_state_t* anim = orch->data.active_animations[j];
                        if (strcmp(anim->filepath, file_info->path) == 0) {
                            // Update existing animation - reset the timer
                            anim->end_time = file_info->last_updated + 30;
                            found = 1;
                            break;
                        }
                    }

                    // If not found, create new animation (skip files that were dirty at startup)
                    if (!found && !was_startup_file(orch, file_info->path) && orch->data.active_animation_count < 100) { // Safety limit
                        animation_state_t* new_anim = create_animation_state(file_info->path, ANIM_SCROLL_LEFT_RIGHT, pane_width);
                        if (new_anim) {
                            // Set timing for runtime animations
                            new_anim->start_time = file_info->last_updated;
                            new_anim->end_time = file_info->last_updated + 30;

                            // Add to animations array (safely)
                            animation_state_t** new_array = realloc(orch->data.active_animations,
                                                                 (orch->data.active_animation_count + 1) * sizeof(animation_state_t*));
                            if (new_array) {
                                orch->data.active_animations = new_array;
                                orch->data.active_animations[orch->data.active_animation_count] = new_anim;
                                orch->data.active_animation_count++;
                            } else {
                                cleanup_animation_state(new_anim);
                            }
                        }
                    }
                }

                // Update scroll positions for all active animations
                for (size_t i = 0; i < orch->data.active_animation_count; i++) {
                    update_animation_state(orch->data.active_animations[i], pane_width, now);
                }

                // Cleanup active files info
                for (size_t i = 0; i < active_file_count; i++) {
                    free(active_files[i].path);
                }
                free(active_files);
            }
            last_git_check = now;  // Reset timer
        }

        // Update any active scroll animations for smooth transitions
        update_scroll_animation(orch);

        // Try to read mouse events first (they start with \033)
        int button, x, y, scroll_delta;
        if (iteration_count <= 3) {
            fprintf(stderr, "DEBUG: About to call read_mouse_event\n");
        }

        int mouse_result = read_mouse_event(&button, &x, &y, &scroll_delta);

        if (iteration_count <= 3) {
            fprintf(stderr, "DEBUG: read_mouse_event returned %d\n", mouse_result);
        }

        if (mouse_result == 0 && width >= 20 && height >= 10) { // Only process mouse events if terminal is valid size
            // Only handle button presses, not releases (button == -1 indicates release)
            if (button == -1) {
                // Button release - ignore to prevent double-clicking
                // Do nothing
            } else {
                // Button press - handle it
                // Convert to 1-based coordinates and validate bounds
                int click_x = x - 1;
                int click_y = y - 1;

                // Bounds check to prevent crashes
                if (click_x < 0 || click_x >= width || click_y < 0 || click_y >= height) {
                    continue; // Invalid coordinates, skip this event
                }

                // Check if click is in footer (last row)
                if (click_y == height - 1) {
                    // Check if click is on the toggle button (around columns 22-32 for "[FLAT]" or "[TREE]")
                    if (click_x >= 22 && click_x <= 32) {
                        // Check cooldown (1 second minimum between clicks)
                        struct timespec now;
                        clock_gettime(CLOCK_MONOTONIC, &now);
                        long elapsed_ms = (now.tv_sec - last_button_click.tv_sec) * 1000 +
                                        (now.tv_nsec - last_button_click.tv_nsec) / 1000000;

                        if (elapsed_ms >= 1000) {
                            // Toggle view mode
                            last_button_click = now;

                            orch->current_view = (orch->current_view == VIEW_FLAT) ? VIEW_TREE : VIEW_FLAT;

                            // Reload data with new view mode for both panes
                            if (load_dirty_files_data(orch, orch->current_view) == 0 &&
                                load_committed_not_pushed_data(orch, orch->current_view) == 0) {
                                // Update scroll states to reflect new data count after view change (pane3 uses animations)
                                get_terminal_size(&width, &height);
                                if (width >= 20 && height >= 10) {
                                    pane_width = width / 3;
                                    if (pane_width < 1) pane_width = 1;
                                    pane_height = height - 5;
                                    update_scroll_state(&orch->data.pane1_scroll, pane_height, orch->data.pane1_count);
                                    update_scroll_state(&orch->data.pane2_scroll, pane_height, orch->data.pane2_count);
                                }
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
                        case 3: scroll_state = NULL; break; // Pane 3 uses animations, no scroll state
                    }
                    if (scroll_state) {
                        // CRITICAL FIX: Throttle scroll updates to prevent rapid accumulation
                        static struct timespec last_scroll_update = {0, 0};
                        static int accumulated_scroll_delta = 0;
                        static int current_fast_scroll_pane = 0;

                        struct timespec now;
                        clock_gettime(CLOCK_MONOTONIC, &now);
                        long elapsed_ms = (now.tv_sec - last_scroll_update.tv_sec) * 1000 +
                                         (now.tv_nsec - last_scroll_update.tv_nsec) / 1000000;

                        // Only update scroll if at least 10ms have passed (prevents rapid accumulation)
                        if (elapsed_ms >= 10) {
                            // Record scroll event for hybrid detection
                            record_scroll_event(scroll_delta);

                            // Check if we're at scroll boundaries to prevent stuttering
                            bool at_boundary = is_at_scroll_boundary(scroll_state, scroll_delta);
                            if (at_boundary) {
                                // At boundary - don't process scroll to prevent stuttering
                                last_scroll_update = now;
                            } else {
                                // Not at boundary - process scroll normally
                                // Use hybrid detection for fast scrolling
                                bool is_fast_scrolling = is_fast_scrolling_detected();

                                if (is_fast_scrolling) {
                                // Fast scrolling: accumulate scroll deltas instead of immediate updates
                                accumulated_scroll_delta += scroll_delta * 4; // Always use 4x for fast scrolling
                                current_fast_scroll_pane = pane_index;

                                // Cancel any existing scroll animation
                                cancel_scroll_animation(orch);

                                // Throttle redraws during fast scrolling (200ms vs 50ms for slow)
                                struct timespec now_redraw;
                                clock_gettime(CLOCK_MONOTONIC, &now_redraw);
                                long elapsed_ms_redraw = (now_redraw.tv_sec - last_redraw.tv_sec) * 1000 +
                                                        (now_redraw.tv_nsec - last_redraw.tv_nsec) / 1000000;

                                if (elapsed_ms_redraw >= 200) { // Longer throttle for fast scrolling
                                    // Calculate target position based on accumulated delta
                                    int target_position = scroll_state->scroll_position + accumulated_scroll_delta;

                                    // Clamp target to valid range
                                    if (target_position < 0) target_position = 0;
                                    if (target_position > scroll_state->max_scroll) target_position = scroll_state->max_scroll;

                                    // Start smooth scroll animation to target position
                                    start_scroll_animation(orch, pane_index, target_position);

                                    // Reset accumulated delta
                                    accumulated_scroll_delta = 0;

                                    draw_tui_overlay(orch);
                                    last_redraw = now_redraw;
                                }
                            } else {
                                // Slow scrolling: immediate updates with normal redraws

                                // Cancel any ongoing fast scroll animation
                                if (current_fast_scroll_pane == pane_index) {
                                    cancel_scroll_animation(orch);
                                    accumulated_scroll_delta = 0;
                                    current_fast_scroll_pane = 0;
                                }

                                // Immediate scroll update
                                update_pane_scroll(scroll_state, scroll_delta, 1);

                                // Immediate redraw for snappy feel
                                struct timespec now_redraw;
                                clock_gettime(CLOCK_MONOTONIC, &now_redraw);
                                long elapsed_ms_redraw = (now_redraw.tv_sec - last_redraw.tv_sec) * 1000 +
                                                        (now_redraw.tv_nsec - last_redraw.tv_nsec) / 1000000;

                                if (elapsed_ms_redraw >= 50) { // Normal 50ms throttle for slow scrolling
                                    draw_tui_overlay(orch);
                                    last_redraw = now_redraw;
                                }
                                }
                            }
                        }

                        last_scroll_update = now;
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

    // Final performance summary
    struct timespec session_end_time;
    clock_gettime(CLOCK_MONOTONIC, &session_end_time);
    double total_session_time = (session_end_time.tv_sec - loop_start_time.tv_sec) +
                               (session_end_time.tv_nsec - loop_start_time.tv_nsec) / 1e9;

    fprintf(stderr, "PERF: SESSION SUMMARY: %.2f seconds, %d iterations (%.1f iter/sec)\n",
             total_session_time, iteration_count, iteration_count / total_session_time);

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