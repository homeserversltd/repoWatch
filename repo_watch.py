#!/usr/bin/env python3
"""
repoWatch - Real-time Git Repository Watcher TUI

A terminal-based application for monitoring git repository changes
with beautiful ASCII animations and intelligent file clustering.
"""

import os
import asyncio
import time
from datetime import datetime, timedelta
from pathlib import Path
from typing import List, Dict, Any, Optional

from textual.app import App, ComposeResult
from textual.containers import Container, Horizontal, Vertical
from textual.widgets import Header, Footer, Static, Tree, ListView, ListItem, Label
from textual.widget import Widget
from textual.binding import Binding
from textual import events

# Import our modules
from styles.theme import get_textual_css
from file_watcher import create_file_watcher, FileChangeEvent
from git_tracker import GitTracker, GitFileStatus, GitCommit
from file_cluster import FileClusterer, cluster_files_for_display
from animations import AnimationEngine


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
        if status_label := self.query_one("#status-text", Label):
            status_label.update(f"Status: {status}")

    def update_repo_info(self, repo_path: str, branch: str):
        """Update repository information."""
        self.repo_path = repo_path
        self.branch = branch
        if repo_label := self.query_one("#repo-info", Label):
            short_path = Path(repo_path).name
            repo_label.update(f"📁 {short_path} [{branch}]")

    def update_session_info(self):
        """Update session duration."""
        duration = datetime.now() - self.session_start
        hours, remainder = divmod(int(duration.total_seconds()), 3600)
        minutes, seconds = divmod(remainder, 60)

        if session_label := self.query_one("#session-info", Label):
            if hours > 0:
                session_label.update(f"⏱️ {hours}h {minutes}m")
            else:
                session_label.update(f"⏱️ {minutes}m {seconds}s")


class FilePane(Widget):
    """Pane showing files in a tree structure."""

    def __init__(self, title: str, pane_id: str):
        super().__init__(id=pane_id)
        self.title = title
        self.files: List[str] = []

    def compose(self) -> ComposeResult:
        yield Container(
            Static(f"[bold]{self.title}[/bold]", classes="pane-title"),
            Tree("No files", id=f"{self.id}-tree"),
            classes="pane"
        )

    def update_files(self, files: List[str], repo_root: Path):
        """Update the file list with clustering."""
        tree = self.query_one(f"#{self.id}-tree", Tree)

        if not files:
            tree.root.label = "No files"
            tree.root.remove_children()
            return

        # Cluster files for display
        display_text = cluster_files_for_display(files, repo_root)

        # Update tree with clustered display
        tree.root.label = f"{self.title} ({len(files)} files)"
        tree.root.remove_children()

        # Simple tree structure for now
        for line in display_text.split('\n'):
            if line.strip():
                tree.root.add(line.strip())


class AnimationPane(Widget):
    """Pane showing ASCII animations."""

    def __init__(self):
        super().__init__(id="animation-pane")
        self.current_animation = ""
        self.last_update = 0

    def compose(self) -> ComposeResult:
        yield Container(
            Static("[bold]Animation Display[/bold]", classes="pane-title"),
            Static("💤 Watching for changes...", id="animation-content", classes="animation-area"),
            classes="pane"
        )

    def update_animation(self, animation_text: str):
        """Update the animation display."""
        content = self.query_one("#animation-content", Static)
        content.update(animation_text)
        self.last_update = time.time()


class RepoWatchApp(App):
    """Main repoWatch TUI application."""

    CSS = get_textual_css()

    BINDINGS = [
        Binding("tab", "focus_next", "Next Pane"),
        Binding("shift+tab", "focus_previous", "Previous Pane"),
        Binding("f1", "show_help", "Help"),
        Binding("f2", "show_settings", "Settings"),
        Binding("ctrl+c", "quit", "Quit"),
    ]

    def __init__(self, repo_path: Path, refresh_interval: float = 1.0):
        super().__init__()
        self.repo_path = repo_path
        self.refresh_interval = refresh_interval

        # Initialize components
        self.git_tracker: Optional[GitTracker] = None
        self.file_watcher: Optional[Any] = None
        self.file_clusterer: Optional[FileClusterer] = None
        self.animation_engine = AnimationEngine()

        # Session tracking
        self.session_start = datetime.now()
        self.last_git_check = 0

        # UI components
        self.status_bar: Optional[StatusBar] = None
        self.uncommitted_pane: Optional[FilePane] = None
        self.committed_pane: Optional[FilePane] = None
        self.animation_pane: Optional[AnimationPane] = None

    def compose(self) -> ComposeResult:
        """Compose the application layout."""
        yield Header()

        with Container(id="main-content"):
            with Horizontal():
                # Left pane - Uncommitted changes
                with Vertical(id="left-pane"):
                    yield FilePane("Uncommitted Changes", "uncommitted")

                # Middle pane - Committed changes
                with Vertical(id="middle-pane"):
                    yield FilePane("Committed (Session)", "committed")

                # Right pane - Animation display
                with Vertical(id="right-pane"):
                    yield AnimationPane()

        yield StatusBar()

    async def on_mount(self) -> None:
        """Initialize the application."""
        try:
            print(f"DEBUG: Initializing repoWatch with repo: {self.repo_path}")

            # Initialize git tracker
            print("DEBUG: Creating GitTracker...")
            self.git_tracker = GitTracker(self.repo_path)
            print("DEBUG: GitTracker created successfully")

            print("DEBUG: Creating FileClusterer...")
            self.file_clusterer = FileClusterer(self.repo_path)
            print("DEBUG: FileClusterer created successfully")

            # Get UI components
            print("DEBUG: Getting UI components...")
            self.status_bar = self.query_one(StatusBar)
            self.uncommitted_pane = self.query_one("#uncommitted", FilePane)
            self.committed_pane = self.query_one("#committed", FilePane)
            self.animation_pane = self.query_one("#animation-pane", AnimationPane)
            print("DEBUG: UI components found")

            # Update status bar
            branch = self.git_tracker.get_branch_name()
            self.status_bar.update_repo_info(str(self.repo_path), branch)
            self.status_bar.update_status("Initializing...")

            # Initialize file watcher
            print("DEBUG: Creating file watcher...")
            self.file_watcher = create_file_watcher(
                self.repo_path,
                self.on_file_change,
                ignore_patterns=['.git', '__pycache__', '.DS_Store']
            )

            if self.file_watcher.start():
                self.status_bar.update_status("Watching for changes")
                print("DEBUG: File watcher started successfully")
            else:
                self.status_bar.update_status("File watching disabled")
                print("DEBUG: File watcher failed to start")

            # Start animation loop
            print("DEBUG: Starting animation loop...")
            asyncio.create_task(self.animation_loop())

            # Initial data load
            print("DEBUG: Loading initial git status...")
            await self.update_git_status()
            self.animation_engine.start_idle_animation()

            print("DEBUG: repoWatch initialization complete!")

        except Exception as e:
            print(f"DEBUG: Error during initialization: {e}")
            import traceback
            traceback.print_exc()
            self.exit(f"Error initializing repoWatch: {e}")

    def on_file_change(self, event: FileChangeEvent):
        """Handle file system change events."""
        # Trigger animation based on change type
        if event.event_type == 'modified':
            self.animation_engine.trigger_file_change('modified', event.relative_path)
        elif event.event_type == 'created':
            self.animation_engine.trigger_file_change('created', event.relative_path)

        # Schedule git status update (don't block the event handler)
        asyncio.create_task(self.update_git_status())

    async def animation_loop(self):
        """Run the animation update loop."""
        while True:
            frame = self.animation_engine.get_current_frame()
            if frame and self.animation_pane:
                self.animation_pane.update_animation(frame)

            await asyncio.sleep(0.1)  # 10 FPS

    async def update_git_status(self):
        """Update git status display."""
        if not self.git_tracker or not self.uncommitted_pane or not self.committed_pane:
            return

        try:
            # Get uncommitted changes
            status_files = self.git_tracker.get_status()
            uncommitted_paths = [f.path for f in status_files]
            self.uncommitted_pane.update_files(uncommitted_paths, self.repo_path)

            # Get committed changes since session start
            recent_commits = self.git_tracker.get_recent_commits(since=self.session_start)
            committed_paths = []
            for commit in recent_commits[:10]:  # Limit to recent commits
                committed_paths.extend(commit.files)

            # Remove duplicates and sort
            committed_paths = list(set(committed_paths))
            self.committed_pane.update_files(committed_paths, self.repo_path)

            # Update status bar
            total_changes = len(uncommitted_paths) + len(committed_paths)
            if total_changes == 0:
                self.status_bar.update_status("Repository clean")
            else:
                self.status_bar.update_status(f"{len(uncommitted_paths)} uncommitted, {len(committed_paths)} committed")

        except Exception as e:
            self.status_bar.update_status(f"Error: {str(e)}")

    async def update_session_timer(self):
        """Update the session timer periodically."""
        while True:
            if self.status_bar:
                self.status_bar.update_session_info()
            await asyncio.sleep(60)  # Update every minute

    async def periodic_git_check(self):
        """Periodically check git status."""
        while True:
            await self.update_git_status()
            await asyncio.sleep(self.refresh_interval)

    async def on_idle(self) -> None:
        """Handle idle events - start background tasks."""
        # Start background tasks
        asyncio.create_task(self.update_session_timer())
        asyncio.create_task(self.periodic_git_check())

    def action_show_help(self):
        """Show help dialog."""
        help_text = """
repoWatch Help

Navigation:
• Tab/Shift+Tab: Switch between panes
• F1: Show this help
• F2: Show settings
• Ctrl+C: Quit

Panes:
• Left: Uncommitted file changes
• Middle: Files committed this session
• Right: ASCII animations for changes

File Clustering:
Files are grouped by parent directory for compact display.
Use Enter to expand/collapse groups.

Press any key to close this help.
        """
        self.notify(help_text, title="Help", timeout=0)

    def action_show_settings(self):
        """Show settings dialog."""
        settings_text = f"""
Current Settings:

Repository: {self.repo_path}
Refresh Interval: {self.refresh_interval}s
Session Start: {self.session_start.strftime('%Y-%m-%d %H:%M:%S')}

File Watcher: {'Active' if self.file_watcher and self.file_watcher.is_active() else 'Inactive'}
        """
        self.notify(settings_text, title="Settings")

    async def on_unmount(self):
        """Clean up resources."""
        if self.file_watcher:
            self.file_watcher.stop()


def main():
    """Main entry point."""
    import sys

    # Get environment variables set by run.sh
    repo_path = os.environ.get('REPO_WATCH_REPO_PATH', '.')
    refresh_interval = float(os.environ.get('REPO_WATCH_REFRESH_INTERVAL', '1.0'))

    # Convert to Path
    repo_path = Path(repo_path).resolve()

    # Create and run app
    app = RepoWatchApp(repo_path, refresh_interval)
    app.run()


if __name__ == "__main__":
    main()
