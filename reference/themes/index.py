#!/usr/bin/env python3
"""
repoWatch Themes Module

Terminal UI theming and color scheme management.
"""

from typing import Dict, Any, Optional
import json
import sys
from pathlib import Path


# Color palette - matches the original web UI design
DEFAULT_COLORS = {
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
DEFAULT_STYLES = """
/* Main application styles */
Screen {
    background: {bg_primary};
    color: {text_primary};
    layout: vertical;
}

Header {
    background: {bg_secondary};
    border-bottom: solid {border_color};
    color: {text_primary};
    padding: 1 2;
    height: 3;
}


#main-layout {
    height: 1fr;  /* Take remaining space minus footer */
}

StatusBar {
    background: {bg_tertiary};
    border-top: solid {border_color};
    color: {text_secondary};
    padding: 0 2;
    height: 3;
}

#keybind-footer {
    background: {bg_secondary};
    border-top: solid {border_color};
    color: {text_secondary};
    padding: 0 2;
    height: 3;
    layout: horizontal;
    dock: bottom;
}

.keybind {
    color: {text_secondary};
    margin-right: 3;
}


/* Pane styles */
Pane {
    background: {bg_secondary};
    border: solid {border_color};
    color: {text_primary};
}

Pane:focus {
    border: solid {accent_blue};
}

.pane-wrapper {
    height: 100%;
}

.pane-title {
    background: {bg_tertiary};
    color: {text_secondary};
    padding: 0 1;
    text-style: bold;
    height: 1;
}

.file-content {
    color: {text_primary};
    padding: 0 1;
    height: 100%;
    overflow: auto;
}

/* Tree/List styles */
Tree {
    background: {bg_secondary};
    color: {text_primary};
}

Tree > .tree--guides {
    color: {border_color};
}

Tree > .tree--cursor {
    background: {bg_tertiary};
    color: {accent_blue};
}

Tree > .tree--highlight {
    background: {accent_blue};
    color: {bg_primary};
}

Tree > .tree--highlight-line {
    background: {bg_tertiary};
}

/* Button styles */
Button {
    background: {bg_tertiary};
    border: solid {border_color};
    color: {text_primary};
    padding: 0 1;
}

Button:hover {
    background: {accent_blue};
    color: {bg_primary};
}

Button:focus {
    border: solid {accent_blue};
}

/* File status indicators */
.modified {
    color: {accent_orange};
}

.staged {
    color: {accent_green};
}

.untracked {
    color: {accent_purple};
}

.committed {
    color: {text_secondary};
}

/* Animation area */
.animation-area {
    background: {bg_primary};
    color: {accent_blue};
    text-align: center;
    padding: 2;
}

/* Cluster groups */
.cluster-group {
    color: {text_secondary};
    text-style: italic;
}

.cluster-count {
    color: {accent_blue};
    text-style: bold;
}
"""


class ThemeManager:
    """Manages terminal UI themes and color schemes."""

    def __init__(self, config: Optional[Dict[str, Any]] = None):
        self.config = config or {}
        self.colors = self._load_colors()
        self.styles = self._load_styles()

    def _load_colors(self) -> Dict[str, str]:
        """Load color palette from configuration."""
        config_colors = self.config.get("config", {}).get("color_palette", {})
        colors = DEFAULT_COLORS.copy()
        colors.update(config_colors)
        return colors

    def _load_styles(self) -> str:
        """Load and format CSS styles with colors."""
        styles = DEFAULT_STYLES

        # Replace color placeholders with actual colors
        for color_name, color_value in self.colors.items():
            styles = styles.replace(f"{{{color_name}}}", color_value)

        return styles

    def get_color(self, name: str) -> str:
        """Get a color by name."""
        return self.colors.get(name, self.colors.get("text_primary", "#ffffff"))

    def get_colors(self) -> Dict[str, str]:
        """Get the complete color palette."""
        return self.colors.copy()

    def get_textual_css(self) -> str:
        """Get the complete Textual CSS stylesheet."""
        return self.styles

    def apply_theme_colors_to_dict(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """Apply theme colors to a dictionary of style values."""
        result = {}
        for key, value in data.items():
            if isinstance(value, str):
                # Replace color placeholders with actual colors
                for color_name, color_value in self.colors.items():
                    value = value.replace(f"{{{color_name}}}", color_value)
            result[key] = value
        return result

    def get_component_style(self, component: str, element: str) -> str:
        """Get a specific component style."""
        components = self.config.get("config", {}).get("component_styles", {})
        component_config = components.get(component, {})
        return component_config.get(element, "")

    def get_textual_style_mapping(self) -> Dict[str, str]:
        """Get mapping of style names to colors for Textual widgets."""
        mapping = self.config.get("config", {}).get("textual_styles", {})
        result = {}

        for style_name, color_name in mapping.items():
            if color_name in self.colors:
                result[style_name] = self.colors[color_name]

        return result


def get_theme_colors(config: Optional[Dict[str, Any]] = None) -> Dict[str, str]:
    """Get the color palette dictionary."""
    manager = ThemeManager(config)
    return manager.get_colors()


def get_textual_css(config: Optional[Dict[str, Any]] = None) -> str:
    """Get the complete Textual CSS stylesheet."""
    manager = ThemeManager(config)
    return manager.get_textual_css()


def apply_theme_colors_to_dict(data: Dict[str, Any], config: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """Apply theme colors to a dictionary of style values."""
    manager = ThemeManager(config)
    return manager.apply_theme_colors_to_dict(data)


class ThemeOrchestrator:
    """Orchestrator for the themes module."""

    def __init__(self, module_path: Path, parent_config: Optional[Dict[str, Any]] = None):
        self.module_path = module_path
        self.parent_config = parent_config or {}
        self.config_path = module_path / "index.json"
        self.config = self._load_config()
        self.theme_manager = ThemeManager(self.config)

    def _load_config(self) -> Dict[str, Any]:
        """Load configuration from index.json."""
        try:
            with open(self.config_path, 'r') as f:
                return json.load(f)
        except FileNotFoundError:
            print(f"Warning: Themes config not found: {self.config_path}")
            return {}
        except json.JSONDecodeError as e:
            print(f"Warning: Invalid themes config JSON: {e}")
            return {}

    def get_theme_manager(self) -> ThemeManager:
        """Get the theme manager instance."""
        return self.theme_manager


def main(module_path: Path, parent_config: Optional[Dict[str, Any]] = None) -> bool:
    """Entry point for the themes module."""
    try:
        orchestrator = ThemeOrchestrator(module_path, parent_config)

        # This module is primarily a library - no standalone execution needed
        # The core module will import and use the theme functionality
        print("Themes module loaded successfully")
        return True

    except Exception as e:
        print(f"Themes module error: {e}")
        import traceback
        traceback.print_exc()
        return False


# Export the main functions and classes for import by other modules
__all__ = ['get_theme_colors', 'get_textual_css', 'apply_theme_colors_to_dict', 'ThemeManager']
