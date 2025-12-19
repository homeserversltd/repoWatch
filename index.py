#!/usr/bin/env python3
"""
repoWatch - Simple Textual TUI

A minimal Textual-based TUI that monitors git repository changes.
Uses inotify for file watching, git for commit tracking, and animations for active changes.
"""

import asyncio
import os
import sys
from datetime import datetime, timedelta
from pathlib import Path

from textual.app import App, ComposeResult
from textual.containers import Container, Horizontal, Vertical
from textual.widgets import Static, Header, Footer

import git
from git import Repo
from watchdog.observers import Observer

try:
    from .file_filter import FileChangeHandler
except ImportError:
    from file_filter import FileChangeHandler


class RepoWatchApp(App):
    """Simple repoWatch Textual TUI."""

    BINDINGS = [
        ("q", "quit_app", "Quit the app"),
        ("escape", "quit_app", "Quit the app"),
        ("ctrl+c", "quit_app", "Quit the app"),
    ]

    CSS = """
    #main-layout {
        layout: vertical;
        height: 100%;
    }

    #main-content {
        height: 1fr;
    }

    .pane {
        width: 1fr;
        height: 100%;
        border: solid $primary;
        margin: 1;
        padding: 1;
    }

    .pane-title {
        text-align: center;
        text-style: bold;
        margin-bottom: 1;
    }

    .file-list {
        height: 100%;
        overflow-y: auto;
    }

    #status-bar {
        dock: bottom;
        height: 1;
        background: $primary;
        color: $text;
        text-align: center;
    }

    Footer {
        dock: bottom;
        height: 1;
        background: $surface;
        color: $text-muted;
        text-align: center;
        border-top: solid $primary;
    }
    """

    def __init__(self, repo_path: Path):
        super().__init__()
        self.repo_path = repo_path
        self.title = f"repoWatch: {repo_path.name}"

        # Initialize components
        self.repo = Repo(repo_path)
        self.observer = None
        self.session_start = datetime.now()
        self.last_commit_time = None  # Track when we last checked commits
        self.active_changes = {}  # file_path -> timestamp
        self.animation_frames = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
        self.frame_index = 0

        # Initialize last_commit_time to 1 hour ago for first run
        self.last_commit_time = datetime.now() - timedelta(hours=1)

    def compose(self) -> ComposeResult:
        """Build the UI layout."""
        yield Header()

        with Vertical(id="main-layout"):
            with Container(id="main-content"):
                with Horizontal():
                    # Left pane - Uncommitted changes
                    with Vertical(classes="pane"):
                        yield Static("[bold]Uncommitted Changes[/bold]", classes="pane-title")
                        yield Static("Loading...", id="uncommitted-files", classes="file-list")

                    # Middle pane - Committed files
                    with Vertical(classes="pane"):
                        yield Static("[bold]Committed Files[/bold]", classes="pane-title")
                        yield Static("Loading...", id="committed-files", classes="file-list")

                    # Right pane - Active changes
                    with Vertical(classes="pane"):
                        yield Static("[bold]Active Changes[/bold]", classes="pane-title")
                        yield Static("💤 Watching...", id="active-changes", classes="file-list")

            yield Static("Ready", id="status-bar")

        yield Footer()

    async def on_mount(self) -> None:
        """Initialize when app starts."""
        # Start file watching
        await self.start_file_watching()

        # Start animation loop
        asyncio.create_task(self.animation_loop())

        # Start commit monitoring loop
        asyncio.create_task(self.commit_monitor_loop())

        # Initial status update
        await self.update_status()

    async def start_file_watching(self):
        """Start inotify file watching."""
        self.observer = Observer()
        handler = FileChangeHandler(self.on_file_change)
        self.observer.schedule(handler, str(self.repo_path), recursive=True)
        self.observer.start()

    def on_file_change(self, event_type: str, file_path: str):
        """Handle file change events."""
        try:
            rel_path = Path(file_path).relative_to(self.repo_path)
            self.active_changes[rel_path] = datetime.now()

            # Schedule UI updates on the main thread
            self.call_later(self.update_active_changes)
            asyncio.create_task(self.update_status())
        except ValueError:
            pass  # File outside repo

    async def animation_loop(self):
        """Run animation loop."""
        while True:
            current_time = datetime.now()

            # Remove files that have been active for more than 1 second
            expired_files = [
                file_path for file_path, timestamp
                in self.active_changes.items()
                if (current_time - timestamp).total_seconds() > 1.0
            ]
            for expired_file in expired_files:
                del self.active_changes[expired_file]

            if self.active_changes:
                self.frame_index += 1
                self.update_active_changes()
            elif expired_files:  # Update display when files expire
                self.update_active_changes()

            await asyncio.sleep(0.1)

    async def commit_monitor_loop(self):
        """Periodically check for new commits."""
        while True:
            try:
                # Check if there are new commits
                new_committed_files = self.get_recent_commits()
                # Always update the committed files pane (even if empty)
                committed_widget = self.query_one("#committed-files", Static)
                if new_committed_files:
                    file_list = "\n".join(f"✅ {file}" for file in new_committed_files)
                    committed_widget.update(file_list)
                else:
                    committed_widget.update("No recent commits")

                # Update status bar
                uncommitted = self.get_uncommitted_files()
                status_widget = self.query_one("#status-bar", Static)
                duration = datetime.now() - self.session_start
                minutes = int(duration.total_seconds() / 60)
                status_widget.update(f"Session: {minutes}m | Uncommitted: {len(uncommitted)} | Committed: {len(new_committed_files)}")
            except Exception as e:
                pass  # Silently handle git errors

            await asyncio.sleep(2.0)  # Check every 2 seconds

    def update_active_changes(self):
        """Update the active changes pane."""
        changes_widget = self.query_one("#active-changes", Static)

        if self.active_changes:
            spinner = self.animation_frames[self.frame_index % len(self.animation_frames)]
            file_list = "\n".join(f"{spinner} {file}" for file in self.active_changes.keys())
            changes_widget.update(f"Active changes:\n{file_list}")
        else:
            changes_widget.update("💤 Watching for changes...")

    def get_uncommitted_files(self):
        """Get list of uncommitted changed files."""
        try:
            staged = [item.a_path for item in self.repo.index.diff('HEAD')]
            unstaged = [item.a_path for item in self.repo.index.diff(None)]
            untracked = [item for item in self.repo.untracked_files]
            return list(set(staged + unstaged + untracked))
        except:
            return []

    def get_recent_commits(self):
        """Get files from recent commits since last check."""
        try:
            # Get the latest commit time we've processed
            if self.last_commit_time is None:
                # First time - get all commits since session start
                since_time = self.session_start
            else:
                # Subsequent times - get commits since last processed time
                since_time = self.last_commit_time

            commits = list(self.repo.iter_commits(
                since=since_time,
                max_count=20  # Increased to catch more commits
            ))

            if commits:
                # Update last commit time to the most recent commit we found
                self.last_commit_time = commits[0].committed_datetime

            committed_files = []
            for commit in commits:
                files = commit.stats.files.keys()
                committed_files.extend(files)

            return list(set(committed_files))
        except Exception as e:
            # Reset last_commit_time on error to force full refresh next time
            self.last_commit_time = None
            return []

    async def update_status(self):
        """Update uncommitted files and status bar."""
        # Update uncommitted files
        uncommitted = self.get_uncommitted_files()
        uncommitted_widget = self.query_one("#uncommitted-files", Static)

        if uncommitted:
            file_list = "\n".join(f"📄 {file}" for file in uncommitted)
            uncommitted_widget.update(file_list)
        else:
            uncommitted_widget.update("No uncommitted changes")

        # Update status bar (committed files are handled by commit_monitor_loop)
        status_widget = self.query_one("#status-bar", Static)
        duration = datetime.now() - self.session_start
        minutes = int(duration.total_seconds() / 60)

        # Get current committed files count (this will be updated by the monitor loop)
        try:
            committed_files = []
            if self.last_commit_time:
                commits = list(self.repo.iter_commits(since=self.session_start, max_count=20))
                for commit in commits:
                    committed_files.extend(commit.stats.files.keys())
            committed_count = len(set(committed_files))
        except:
            committed_count = 0

        status_widget.update(f"Session: {minutes}m | Uncommitted: {len(uncommitted)} | Committed: {committed_count}")

    def action_quit_app(self) -> None:
        """Quit the application."""
        self.exit()

    async def on_unmount(self):
        """Clean up resources."""
        if self.observer:
            self.observer.stop()
            self.observer.join()


def check_dependencies():
    """Check and install required dependencies."""
    required_deps = ["textual", "watchdog", "gitpython"]

    missing_deps = []
    for dep in required_deps:
        try:
            if dep == "gitpython":
                import git
            else:
                __import__(dep)
        except ImportError:
            missing_deps.append(dep)

    if missing_deps:
        print(f"Installing missing dependencies: {', '.join(missing_deps)}")
        try:
            import subprocess
            for dep in missing_deps:
                print(f"Installing {dep}...")
                subprocess.check_call([
                    sys.executable, "-m", "pip", "install",
                    "--break-system-packages", dep
                ])
            print("Dependencies installed successfully.")
        except subprocess.CalledProcessError as e:
            print(f"Failed to install dependencies: {e}")
            return False

    return True


def main(module_path: Path = None, parent_config: dict = None):
    """Main entry point."""
    # Check dependencies
    if not check_dependencies():
        print("Failed to install dependencies")
        return False

    # Get repo path from environment or use current directory
    repo_path_str = os.environ.get('REPO_WATCH_REPO_PATH', '.')
    repo_path = Path(repo_path_str).resolve()

    if not repo_path.exists():
        print(f"Error: Repository path does not exist: {repo_path}")
        return False

    if not (repo_path / '.git').exists():
        print(f"Error: Not a git repository: {repo_path}")
        return False

    print(f"Starting repoWatch for repository: {repo_path}")

    # Create and run the app
    app = RepoWatchApp(repo_path)
    app.run()

    return True


if __name__ == "__main__":
    # When run directly, execute the app
    success = main()
    sys.exit(0 if success else 1)