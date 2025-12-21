#include "../three-pane-tui.h"
#include <math.h>

// Create a new animation state for a file
animation_state_t* create_animation_state(const char* filepath, animation_type_t type, int pane_width) {
    if (!filepath) return NULL;

    animation_state_t* anim = calloc(1, sizeof(animation_state_t));
    if (!anim) return NULL;

    anim->type = type;
    anim->filepath = strdup(filepath);
    if (!anim->filepath) {
        free(anim);
        return NULL;
    }

    time_t now = time(NULL);
    anim->start_time = now;
    anim->end_time = now + 30;  // 30 second duration
    anim->scroll_position = 0;
    anim->pane_width = pane_width;

    return anim;
}

// Update animation state (advance scroll position, etc.)
void update_animation_state(animation_state_t* anim, int pane_width, time_t now) {
    if (!anim) return;

    // Update pane width in case it changed
    anim->pane_width = pane_width;

    // For scroll animations, advance the scroll position
    if (anim->type == ANIM_SCROLL_LEFT_RIGHT) {
        // Increment scroll position by 1 character per frame
        // This will create the scrolling effect when used in the render function
        anim->scroll_position++;
    }
}

// Render scrolling left-to-right animation (Pac-Man style loop)
void render_scroll_left_right(animation_state_t* anim, int row, int start_col, int width) {
    if (!anim || !anim->filepath) return;

    // Calculate text width
    int text_width = get_string_display_width(anim->filepath);

    // Calculate available width (leave 1 char padding on each side)
    int available_width = width - 2;
    if (available_width <= 0) return;

    // Create scrolling loop: text enters from right, scrolls left, exits left, re-enters from right
    // Formula: display_start = (scroll_position % (available_width + text_width)) - text_width
    int cycle_length = available_width + text_width;
    int relative_pos = anim->scroll_position % cycle_length;
    int display_start = relative_pos - text_width;

    // Only render if the text is visible within our pane bounds
    if (display_start < -text_width || display_start >= available_width) {
        return; // Text is completely off-screen
    }

    // Move cursor to the row
    move_cursor(row, start_col + 1); // +1 for left padding

    // Render the visible portion of the text
    int pane_start_col = 0;
    int pane_end_col = available_width - 1;

    for (int pane_col = pane_start_col; pane_col <= pane_end_col; pane_col++) {
        int text_pos = pane_col - display_start;

        if (text_pos >= 0 && text_pos < text_width) {
            // Get the character at this position in the filepath
            // For simplicity, we'll use byte indexing (not perfect for UTF-8 but works for ASCII filenames)
            if (text_pos < (int)strlen(anim->filepath)) {
                putchar(anim->filepath[text_pos]);
            }
        } else {
            // Space padding
            putchar(' ');
        }
    }
}

// Check if animation has expired
int is_animation_expired(animation_state_t* anim, time_t now) {
    if (!anim) return 1;
    return (now >= anim->end_time);
}

// Clean up animation state
void cleanup_animation_state(animation_state_t* anim) {
    if (!anim) return;

    free(anim->filepath);
    free(anim);
}

