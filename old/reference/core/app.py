#!/usr/bin/env python3
"""
repoWatch Main Application

Main TUI application class for repoWatch.
"""

import asyncio
import time
from datetime import datetime
from pathlib import Path
from typing import Optional, Any

from textual.app import App, ComposeResult
from textual.containers import Container, Horizontal, Vertical

from git_tracker.index import GitTracker, GitCommit
from watcher.index import create_file_watcher, FileChangeEvent
from display.index import FileClusterer, AnimationEngine
from themes.index import get_textual_css
import widgets


class RepoWatchApp(App):
    """Main repoWatch TUI application."""

    CSS = get_textual_css()

    BINDINGS = [
        ("tab", "focus_next", "Next Pane"),
        ("shift+tab", "focus_previous", "Previous Pane"),
        ("f1", "show_help", "Help"),
        ("f2", "show_settings", "Settings"),
        ("q", "quit_app", "Quit"),
        ("escape", "quit_app", "Quit"),
        ("ctrl+c", "quit_app", "Quit"),
    ]

    def action_quit_app(self) -> None:
        """Quit the application gracefully."""
        # Stop file watcher if running
        if hasattr(self, 'file_watcher') and self.file_watcher:
            try:
                self.file_watcher.stop()
            except Exception:
                pass
        # Exit gracefully using Textual's built-in exit method
        self.exit()

    def record_user_activity(self):
        """Record that user performed an action."""
        self.last_user_activity = time.time()

    async def check_inactivity_timeout(self):
        """Check for inactivity and auto-quit if needed."""
        while True:
            current_time = time.time()
            if current_time - self.last_user_activity > self.auto_quit_timeout:
                print(f"\nAuto-quitting after {self.auto_quit_timeout} seconds of inactivity...")
                self.action_quit()
                break
            await asyncio.sleep(60)  # Check every minute

    def __init__(self, repo_path: Path, refresh_interval: float = 1.0):
        super().__init__()
        self.repo_path = repo_path
        self.refresh_interval = refresh_interval
        # Set title so Header can display it
        self.title = f"repoWatch: {repo_path.name}"

        # Initialize components
        self.git_tracker: Optional[GitTracker] = None
        self.file_watcher: Optional[Any] = None
        self.file_clusterer: Optional[FileClusterer] = None
        self.animation_engine = AnimationEngine()

        # Session tracking
        self.session_start = datetime.now()
        self.last_git_check = 0
        self.last_user_activity = time.time()
        self.auto_quit_timeout = 300  # 5 minutes of inactivity

        # UI components
        self.status_bar: Optional[widgets.StatusBar] = None
        self.uncommitted_pane: Optional[widgets.FilePane] = None
        self.committed_pane: Optional[widgets.FilePane] = None
        self.animation_pane: Optional[widgets.AnimationPane] = None

    def compose(self) -> ComposeResult:
        """Compose the application layout."""
        # Header automatically displays self.title
        from textual.widgets import Header
        yield Header()

        # Main content area with constrained height to leave room for footer
        with Vertical(id="main-layout"):
            with Container(id="main-content"):
                with Horizontal():
                    # Left pane - Uncommitted changes
                    with Vertical(id="left-pane"):
                        yield widgets.FilePane("Uncommitted Changes", "uncommitted")

                    # Middle pane - Committed changes
                    with Vertical(id="middle-pane"):
                        yield widgets.FilePane("Committed (Session)", "committed")

                    # Right pane - Animation display
                    with Vertical(id="right-pane"):
                        yield widgets.AnimationPane()

            yield widgets.StatusBar()

        # Footer with explicit docking
        yield widgets.KeybindFooter()

    async def on_mount(self) -> None:
        """Initialize the application."""
        try:
            print(f"DEBUG: ===== STARTING repoWatch with repo: {self.repo_path} =====", flush=True)

            # Initialize git tracker
            print("DEBUG: Creating GitTracker...")
            self.git_tracker = GitTracker(self.repo_path)
            print("DEBUG: GitTracker created successfully")

            print("DEBUG: Creating FileClusterer...")
            self.file_clusterer = FileClusterer(self.repo_path)
            print("DEBUG: FileClusterer created successfully")

            # Get UI components - wait a moment for widgets to mount
            await asyncio.sleep(0.1)
            self.status_bar = self.query_one(widgets.StatusBar)
            self.uncommitted_pane = self.query_one("#uncommitted", widgets.FilePane)
            self.committed_pane = self.query_one("#committed", widgets.FilePane)
            self.animation_pane = self.query_one("#animation-pane", widgets.AnimationPane)

            # Ensure app can receive keyboard focus
            self.screen.can_focus = True

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
        if not self.git_tracker or not self.uncommitted_pane or not self.committed_pane or not self.status_bar:
            return

        try:
            # Get uncommitted changes
            status_files = self.git_tracker.get_status()
            uncommitted_paths = [f.path for f in status_files]
            if self.uncommitted_pane:
                self.uncommitted_pane.update_files(uncommitted_paths, self.repo_path)

            # Get committed changes since session start
            recent_commits = self.git_tracker.get_recent_commits(since=self.session_start)
            committed_paths = []
            for commit in recent_commits[:10]:  # Limit to recent commits
                committed_paths.extend(commit.files)

            # Remove duplicates and sort
            committed_paths = list(set(committed_paths))
            if self.committed_pane:
                self.committed_pane.update_files(committed_paths, self.repo_path)

            # Update status bar
            total_changes = len(uncommitted_paths) + len(committed_paths)
            if self.status_bar:
                if total_changes == 0:
                    self.status_bar.update_status("Repository clean")
                else:
                    self.status_bar.update_status(f"{len(uncommitted_paths)} uncommitted, {len(committed_paths)} committed")

        except Exception as e:
            # Silently handle errors - widgets might not be mounted yet
            pass

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
        self.record_user_activity()
        help_text = """
repoWatch Help

Navigation:
• Tab/Shift+Tab: Switch between panes
• F1: Show this help
• F2: Show settings

Quit Application:
• q, ESC, or Ctrl+C: Quit the application

Panes:
• Left: Uncommitted file changes
• Middle: Files committed this session
• Right: ASCII animations for changes

File Display:
Files are shown individually as a flat list.
Each file appears on its own line for maximum visibility.

Press any key to close this help.
        """
        self.notify(help_text, title="Help", timeout=0)

    def action_show_settings(self):
        """Show settings dialog."""
        self.record_user_activity()
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