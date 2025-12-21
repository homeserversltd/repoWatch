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
    style_config_t styles;
} three_pane_tui_config_t;

// Hardcoded data for the three panes
typedef struct {
    char** pane1_items;
    size_t pane1_count;
    char** pane2_items;
    size_t pane2_count;
    char** pane3_items;
    size_t pane3_count;
} three_pane_data_t;

// Orchestrator for three-pane-tui module
typedef struct {
    char* module_path;
    three_pane_tui_config_t config;
    three_pane_data_t data;
} three_pane_tui_orchestrator_t;

// Global flag for redraw requests
extern volatile sig_atomic_t redraw_needed;

// Function declarations
void handle_sigwinch(int sig);

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

// Styles module functions
int get_file_color(const char* filepath, const style_config_t* styles);
int load_styles(style_config_t* styles, const char* module_path);

// Data module functions
int load_git_submodules_data(three_pane_tui_orchestrator_t* orch);
int load_committed_not_pushed_data(three_pane_tui_orchestrator_t* orch);
int load_dirty_files_data(three_pane_tui_orchestrator_t* orch);
int load_hardcoded_data(three_pane_tui_orchestrator_t* orch);

// UI module functions
void draw_pane(int start_col, int width, int height, const char* title, char** items, size_t item_count, int title_color, const style_config_t* styles, int pane_index);
void draw_tui_overlay(three_pane_tui_orchestrator_t* orch);

// Main module functions
three_pane_tui_orchestrator_t* three_pane_tui_init(const char* module_path);
void three_pane_tui_cleanup(three_pane_tui_orchestrator_t* orch);
int three_pane_tui_execute(three_pane_tui_orchestrator_t* orch);
int load_config(three_pane_tui_orchestrator_t* orch);

#endif // THREE_PANE_TUI_H
