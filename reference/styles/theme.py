"""
repoWatch Theme System

Dark terminal theme with accent colors, plundered from the original web UI.
"""

from typing import Dict, Any

# Color palette - matches the original web UI design
COLORS = {
    "bg_primary": "#0d1117",
    "bg_secondary": "#161b22",
    "bg_tertiary": "#21262d",
    "text_primary": "#e6edf3",
    "text_secondary": "#8b949e",
    "text_muted": "#6e7681",
    "border_color": "#30363d",
    "accent_blue": "#58a6ff",
    "accent_green": "#3fb950",
    "accent_purple": "#a371f7",
    "accent_orange": "#d29922",
}

# Textual CSS styles
STYLES = f"""
/* Main application styles */
Screen {{
    background: {COLORS['bg_primary']};
    color: {COLORS['text_primary']};
}}

Header {{
    background: {COLORS['bg_secondary']};
    border-bottom: solid {COLORS['border_color']};
    color: {COLORS['text_primary']};
    padding: 1 2;
}}

StatusBar {{
    background: {COLORS['bg_tertiary']};
    border-top: solid {COLORS['border_color']};
    color: {COLORS['text_secondary']};
    padding: 0 2;
}}

/* Pane styles */
Pane {{
    background: {COLORS['bg_secondary']};
    border: solid {COLORS['border_color']};
    color: {COLORS['text_primary']};
}}

Pane:focus {{
    border: solid {COLORS['accent_blue']};
}}

PaneTitle {{
    background: {COLORS['bg_tertiary']};
    color: {COLORS['text_secondary']};
    padding: 0 1;
    text-style: bold;
}}

/* Tree/List styles */
Tree {{
    background: {COLORS['bg_secondary']};
    color: {COLORS['text_primary']};
}}

Tree > .tree--guides {{
    color: {COLORS['border_color']};
}}

Tree > .tree--cursor {{
    background: {COLORS['bg_tertiary']};
    color: {COLORS['accent_blue']};
}}

Tree > .tree--highlight {{
    background: {COLORS['accent_blue']};
    color: {COLORS['bg_primary']};
}}

Tree > .tree--highlight-line {{
    background: {COLORS['bg_tertiary']};
}}

/* Button styles */
Button {{
    background: {COLORS['bg_tertiary']};
    border: solid {COLORS['border_color']};
    color: {COLORS['text_primary']};
    padding: 0 1;
}}

Button:hover {{
    background: {COLORS['accent_blue']};
    color: {COLORS['bg_primary']};
}}

Button:focus {{
    border: solid {COLORS['accent_blue']};
}}

/* File status indicators */
.modified {{
    color: {COLORS['accent_orange']};
}}

.staged {{
    color: {COLORS['accent_green']};
}}

.untracked {{
    color: {COLORS['accent_purple']};
}}

.committed {{
    color: {COLORS['text_secondary']};
}}

/* Animation area */
.animation-area {{
    background: {COLORS['bg_primary']};
    color: {COLORS['accent_blue']};
    text-align: center;
    padding: 2;
}}

/* Cluster groups */
.cluster-group {{
    color: {COLORS['text_secondary']};
    text-style: italic;
}}

.cluster-count {{
    color: {COLORS['accent_blue']};
    text-style: bold;
}}
"""

def get_theme_colors() -> Dict[str, str]:
    """Get the color palette dictionary."""
    return COLORS.copy()

def get_textual_css() -> str:
    """Get the complete Textual CSS stylesheet."""
    return STYLES

def apply_theme_colors_to_dict(data: Dict[str, Any]) -> Dict[str, Any]:
    """Apply theme colors to a dictionary of style values."""
    result = {}
    for key, value in data.items():
        if isinstance(value, str):
            # Replace color placeholders with actual colors
            for color_name, color_value in COLORS.items():
                value = value.replace(f"{{COLORS['{color_name}']}}", color_value)
        result[key] = value
    return result
