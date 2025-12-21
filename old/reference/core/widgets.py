#!/usr/bin/env python3
"""
repoWatch UI Widgets

Collection of Textual UI widgets used by the repoWatch application.
"""

import time
from datetime import datetime
from pathlib import Path
from typing import List

from textual.app import ComposeResult
from textual.containers import Container, Vertical
from textual.widget import Widget
from textual.widgets import Static, Label

# Import display functions
from display.index import display_all_files_flat


class StatusBar(Widget):
    """Status bar showing repository information."""

    def __init__(self):
        super().__init__()
        self.repo_path = ""
        self.branch = ""
        self.status = ""
        self.session_start = datetime.now()

    def compose(self) -> ComposeResult:
        yield Container(
            Label("Status: Ready", id="status-text"),
            Label("", id="repo-info"),
            Label("", id="session-info"),
            id="status-bar"
        )

    def update_status(self, status: str):
        """Update the status text."""
        try:
            status_labels = self.query("#status-text", Label)
            if status_labels:
                status_labels.first().update(f"Status: {status}")
        except Exception:
            pass  # Widget not mounted yet

    def update_repo_info(self, repo_path: str, branch: str):
        """Update repository information."""
        self.repo_path = repo_path
        self.branch = branch
        try:
            repo_labels = self.query("#repo-info", Label)
            if repo_labels:
                # Don't show repo path - just show branch
                repo_labels.first().update(f"[{branch}]")
        except Exception:
            pass  # Widget not mounted yet

    def update_session_info(self):
        """Update session duration."""
        duration = datetime.now() - self.session_start
        hours, remainder = divmod(int(duration.total_seconds()), 3600)
        minutes, seconds = divmod(remainder, 60)

        try:
            session_labels = self.query("#session-info", Label)
            if session_labels:
                session_label = session_labels.first()
                if hours > 0:
                    session_label.update(f"⏱️ {hours}h {minutes}m")
                else:
                    session_label.update(f"⏱️ {minutes}m {seconds}s")
        except Exception:
            pass  # Widget not mounted yet


class FilePane(Widget):
    """Pane showing files as a scrollable list."""

    can_focus = True

    def __init__(self, title: str, pane_id: str):
        super().__init__(id=pane_id)
        self.title = title
        self.files: List[str] = []
        self.file_widgets: List[Static] = []

    def compose(self) -> ComposeResult:
        yield Vertical(
            Static(f"[bold]{self.title}[/bold]", classes="pane-title"),
            Static("No files yet", id=f"{self.id}-files", classes="file-content", expand=True),
            classes="pane-wrapper"
        )

    def update_files(self, files: List[str], repo_root: Path):
        """Update the file list showing all files individually."""
        print(f"DEBUG: update_files called for {self.id} with {len(files)} files", flush=True)

        try:
            files_widgets = self.query(f"#{self.id}-files", Static)
            if not files_widgets:
                print(f"DEBUG: ERROR - files_widget not found for {self.id}", flush=True)
                return
            files_widget = files_widgets.first()
        except Exception as e:
            print(f"DEBUG: Exception querying files_widget: {e}", flush=True)
            return

        print(f"DEBUG: Found files_widget: {files_widget}", flush=True)

        if not files:
            print("DEBUG: No files to display", flush=True)
            files_widget.update("No files")
            return

        # Ensure all paths are relative to repo root (strip any absolute paths)
        relative_files = []
        for file_path in files:
            try:
                path = Path(file_path)
                if path.is_absolute():
                    # Convert absolute path to relative
                    try:
                        rel_path = path.relative_to(repo_root)
                        relative_files.append(str(rel_path))
                    except ValueError:
                        # File outside repo, skip it
                        continue
                else:
                    # Already relative, use as-is
                    relative_files.append(file_path)
            except (ValueError, OSError):
                continue

        # Display all files as flat list
        display_text = display_all_files_flat(relative_files, repo_root)

        print(f"DEBUG: display_all_files_flat returned: {len(display_text)} chars", flush=True)
        print(f"DEBUG: display_text[:100]: {repr(display_text[:100])}", flush=True)

        # TEMPORARY: Print files directly to console to verify processing
        print(f"\n=== {self.title} ===", flush=True)
        if relative_files:
            for i, file in enumerate(relative_files, 1):
                print(f"{i}. 📄 {file}", flush=True)
        else:
            print("No files", flush=True)
        print("=" * 50, flush=True)

        # Update the single static widget with all files
        final_text = f"FILES:\n{display_text}" if display_text.strip() else "No files to display"
        print(f"DEBUG: Updating widget with: {repr(final_text[:100])}", flush=True)
        files_widget.update(final_text)
        print("DEBUG: Widget update completed", flush=True)

        # TEMPORARY: Include file names in title to verify processing
        file_names = ", ".join(relative_files[:3])  # Show first 3 files
        if len(relative_files) > 3:
            file_names += f" (+{len(relative_files)-3} more)"

        # Update title (find the title widget and update it)
        try:
            title_widgets = self.query(".pane-title", Static)
            if title_widgets:
                title_widgets.first().update(f"[bold]{self.title} ({len(relative_files)} files): {file_names}[/bold]")
        except Exception:
            pass  # Widget not mounted yet


class AnimationPane(Widget):
    """Pane showing ASCII animations."""

    can_focus = True

    def __init__(self):
        super().__init__(id="animation-pane")
        self.current_animation = ""
        self.last_update = 0

    def compose(self) -> ComposeResult:
        yield Vertical(
            Static("[bold]Animation Display[/bold]", classes="pane-title"),
            Static("💤 Watching for changes...", id="animation-content", classes="animation-area", expand=True),
            classes="pane-wrapper"
        )

    def update_animation(self, animation_text: str):
        """Update the animation display."""
        content = self.query_one("#animation-content", Static)
        content.update(animation_text)
        self.last_update = time.time()


class KeybindFooter(Widget):
    """Footer displaying keyboard shortcuts."""

    def compose(self) -> ComposeResult:
        yield Container(
            Label("Tab: Next", classes="keybind"),
            Label("Shift+Tab: Prev", classes="keybind"),
            Label("F1: Help", classes="keybind"),
            Label("F2: Settings", classes="keybind"),
            Label("Q: Quit", classes="keybind"),
            Label("Ctrl+C: Force", classes="keybind"),
            id="keybind-footer"
        )

