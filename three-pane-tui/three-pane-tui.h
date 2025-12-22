#ifndef THREE_PANE_TUI_H
#define THREE_PANE_TUI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "../json-utils/json-utils.h"

// View mode enumeration for file display
typedef enum {
    VIEW_FLAT,
    VIEW_TREE
} view_mode_t;

// Animation types enumeration
typedef enum {
    ANIM_SCROLL_LEFT_RIGHT
    // Future: ANIM_PULSE, ANIM_BLINK, etc.
} animation_type_t;

// Style configuration for file colorization
typedef struct {
    int directory_color;
    int file_default_color;
    char** extensions;
    int* extension_colors;
    size_t extension_count;
    char** special_files;
    int* special_file_colors;
    size_t special_file_count;
} file_style_config_t;

// UI color configuration
typedef struct {
    int title_color;
    int header_separator_color;
    struct {
        int left;
        int center;
        int right;
    } pane_titles;
    struct {
        int vertical;
        int horizontal;
    } borders;
    struct {
        int separator;
        int text;
    } footer;
} ui_color_config_t;

// Complete style configuration
typedef struct {
    file_style_config_t files;
    ui_color_config_t ui;
} style_config_t;

// Configuration for three-pane-tui module
typedef struct {
    char* title;
    char* exit_keys;
    char* pane1_title;
    char* pane2_title;
    char* pane3_title;
    view_mode_t default_view;
    style_config_t styles;
} three_pane_tui_config_t;

// Scroll state for individual panes
typedef struct {
    int scroll_position;     // Current scroll position (0 = top)
    int max_scroll;         // Maximum scroll position
    int viewport_height;    // Number of visible lines in pane
    int total_items;        // Total number of items in pane
} pane_scroll_state_t;

// Scroll animation state for smooth transitions
typedef struct {
    int is_animating;           // Whether animation is currently active
    int start_position;         // Starting scroll position
    int target_position;        // Target scroll position
    struct timespec start_time; // When animation started
    double duration_sec;        // Total animation duration
    int pane_index;            // Which pane is animating (1, 2, or 3)
} scroll_animation_t;

// Animation state structure
typedef struct {
    animation_type_t type;
    char* filepath;
    time_t start_time;
    time_t end_time;  // start_time + 30 seconds
    int scroll_position;  // For scroll animations
    int pane_width;  // Cached pane width for calculations
} animation_state_t;

// Data for the three panes (pane3 uses animations instead of hardcoded items)
typedef struct {
    char** pane1_items;
    size_t pane1_count;
    char** pane2_items;
    size_t pane2_count;
    animation_state_t** active_animations;  // Active file change animations for pane 3
    size_t active_animation_count;
    char** startup_files;  // Files that were dirty at startup (don't animate)
    size_t startup_file_count;
    pane_scroll_state_t pane1_scroll;
    pane_scroll_state_t pane2_scroll;
    // pane3_scroll removed - animations don't use scroll state
    scroll_animation_t scroll_animation;  // Scroll animation state for smooth transitions
} three_pane_data_t;

// Orchestrator for three-pane-tui module
typedef struct {
    char* module_path;
    three_pane_tui_config_t config;
    three_pane_data_t data;
    view_mode_t current_view;
} three_pane_tui_orchestrator_t;

// Global flag for redraw requests
extern volatile sig_atomic_t redraw_needed;

// Function declarations
void handle_sigwinch(int sig);
void emergency_cleanup(int sig);

// Core module functions
void save_cursor_position();
void restore_cursor_position();
void hide_cursor();
void show_cursor();
void clear_screen();
void move_cursor(int row, int col);
void reset_colors();
void set_bold();
void set_color(int color_code);
void set_background(int color_code);
int get_terminal_size(int* width, int* height);
char* expandvars(const char* input);
int enable_mouse_reporting();
void disable_mouse_reporting();
int read_mouse_event(int* button, int* x, int* y, int* scroll_delta);
int read_char_timeout();
int get_string_display_width(const char* str);
char* truncate_string_right_priority(const char* str, int max_width);

// Styles module functions
int get_file_color(const char* filepath, const style_config_t* styles);
int get_repo_color(const char* repo_name);
int get_repo_color_index(const char* repo_name);
int color_index_to_ansi(int index);
void adjust_colors_no_touching(int* colors, size_t count);
int load_styles(style_config_t* styles, const char* module_path);

// Structure to hold active file information
typedef struct {
    char* path;
    time_t last_updated;
} active_file_info_t;

// Data module functions
int load_git_submodules_data(three_pane_tui_orchestrator_t* orch);
int load_committed_not_pushed_data(three_pane_tui_orchestrator_t* orch, view_mode_t view_mode);
int load_dirty_files_data(three_pane_tui_orchestrator_t* orch, view_mode_t view_mode);
active_file_info_t* load_file_changes_data(size_t* active_count);

// Animation module functions
animation_state_t* create_animation_state(const char* filepath, animation_type_t type, int pane_width);
void update_animation_state(animation_state_t* anim, int pane_width, time_t now);
void render_scroll_left_right(animation_state_t* anim, int row, int start_col, int width);
int is_animation_expired(animation_state_t* anim, time_t now);
void cleanup_animation_state(animation_state_t* anim);

// Scroll animation functions
void start_scroll_animation(three_pane_tui_orchestrator_t* orch, int pane_index, int target_position);
void update_scroll_animation(three_pane_tui_orchestrator_t* orch);
int is_scroll_animation_active(three_pane_tui_orchestrator_t* orch);
void cancel_scroll_animation(three_pane_tui_orchestrator_t* orch);

// UI module functions
void draw_pane(int start_col, int width, int height, const char* title, char** items, size_t item_count, int title_color, const style_config_t* styles, int pane_index, const pane_scroll_state_t* scroll_state, three_pane_tui_orchestrator_t* orch);
void draw_tui_overlay(three_pane_tui_orchestrator_t* orch);
int get_pane_at_position(int x, int y, int pane_width, int total_width, int pane_height);
void update_pane_scroll(pane_scroll_state_t* scroll_state, int direction, int amount);
void update_scroll_state(pane_scroll_state_t* scroll_state, int viewport_height, int total_items);

// Main module functions
three_pane_tui_orchestrator_t* three_pane_tui_init(const char* module_path);
void three_pane_tui_cleanup(three_pane_tui_orchestrator_t* orch);
int three_pane_tui_execute(three_pane_tui_orchestrator_t* orch);
int load_config(three_pane_tui_orchestrator_t* orch);
int was_startup_file(three_pane_tui_orchestrator_t* orch, const char* filepath);

#endif // THREE_PANE_TUI_H
