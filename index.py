#!/usr/bin/env python3
"""
repoWatch - Simple Textual TUI

A minimal Textual-based TUI that monitors git repository changes.
Uses inotify for file watching, git for commit tracking, and animations for active changes.
"""

import asyncio
import os
import sys
from datetime import datetime
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
        self.active_changes = {}  # file_path -> timestamp
        self.animation_frames = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
        self.frame_index = 0

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

            # Update status
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
        """Get files from recent commits."""
        try:
            commits = list(self.repo.iter_commits(
                since=self.session_start,
                max_count=10
            ))

            committed_files = []
            for commit in commits:
                files = commit.stats.files.keys()
                committed_files.extend(files)

            return list(set(committed_files))
        except:
            return []

    async def update_status(self):
        """Update all panes."""
        # Update uncommitted files
        uncommitted = self.get_uncommitted_files()
        uncommitted_widget = self.query_one("#uncommitted-files", Static)

        if uncommitted:
            file_list = "\n".join(f"📄 {file}" for file in uncommitted)
            uncommitted_widget.update(file_list)
        else:
            uncommitted_widget.update("No uncommitted changes")

        # Update committed files
        committed = self.get_recent_commits()
        committed_widget = self.query_one("#committed-files", Static)

        if committed:
            file_list = "\n".join(f"✅ {file}" for file in committed)
            committed_widget.update(file_list)
        else:
            committed_widget.update("No recent commits")

        # Update status bar
        status_widget = self.query_one("#status-bar", Static)
        duration = datetime.now() - self.session_start
        minutes = int(duration.total_seconds() / 60)
        status_widget.update(f"Session: {minutes}m | Uncommitted: {len(uncommitted)} | Committed: {len(committed)}")

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
