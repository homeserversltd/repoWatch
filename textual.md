# Building a Textual TUI for repoWatch

> *"The simplest solution that works is better than a complex solution that doesn't."*

This guide shows how to build a **working Textual TUI** for repoWatch. Forget the Matryoshka pattern, forget complex abstractions. We're building something that actually displays content using Textual's layout and styling system.

## The Core Requirements

Our repoWatch TUI needs to:

1. **Watch a directory** with inotify (file system events)
2. **Display changed files** in a column (uncommitted changes)
3. **Display committed files** in a column (from git history)
4. **Show active changes** in a column (with animations)

## Textual Fundamentals

Textual TUI framework gives us:

- **App class** - Main application container
- **Containers** - Layout management (Horizontal, Vertical)
- **Widgets** - UI components (Static, Label, etc.)
- **CSS styling** - Terminal styling system
- **Event system** - Async event handling

## Simple Textual App Structure

```python
#!/usr/bin/env python3
"""
Simple repoWatch Textual TUI
"""

import asyncio
from pathlib import Path
from textual.app import App, ComposeResult
from textual.containers import Container, Horizontal, Vertical
from textual.widgets import Static, Header, Footer

class RepoWatchApp(App):
    """Simple repoWatch TUI application."""

    # CSS styling (inline for simplicity)
    CSS = """
    #main-content {
        height: 100%;
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
    """

    def __init__(self, repo_path: Path):
        super().__init__()
        self.repo_path = repo_path
        self.title = f"repoWatch: {repo_path.name}"

    def compose(self) -> ComposeResult:
        """Build the UI layout."""
        yield Header()

        with Container(id="main-content"):
            with Horizontal():
                # Left pane - Uncommitted changes
                with Vertical(classes="pane"):
                    yield Static("[bold]Uncommitted Changes[/bold]", classes="pane-title")
                    yield Static("No changes", id="uncommitted-files", classes="file-list")

                # Middle pane - Committed files
                with Vertical(classes="pane"):
                    yield Static("[bold]Committed Files[/bold]", classes="pane-title")
                    yield Static("No commits", id="committed-files", classes="file-list")

                # Right pane - Active changes
                with Vertical(classes="pane"):
                    yield Static("[bold]Active Changes[/bold]", classes="pane-title")
                    yield Static("💤 Watching...", id="active-changes", classes="file-list")

        yield Footer()

    async def on_mount(self) -> None:
        """Initialize when app starts."""
        # Start watching files
        await self.start_file_watching()

        # Start git monitoring
        await self.start_git_monitoring()

    async def start_file_watching(self):
        """Start inotify file watching."""
        # We'll implement this with watchdog
        pass

    async def start_git_monitoring(self):
        """Start git status monitoring."""
        # We'll implement this with GitPython
        pass

def main():
    """Main entry point."""
    import sys

    repo_path = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()

    app = RepoWatchApp(repo_path)
    app.run()

if __name__ == "__main__":
    main()
```

## The Three-Pane Layout

Textual uses **containers** to create layouts:

```python
def compose(self) -> ComposeResult:
    """Build the UI layout."""
    yield Header()

    # Main content in a container
    with Container(id="main-content"):
        # Horizontal layout for three panes
        with Horizontal():
            # Left pane
            with Vertical(classes="pane"):
                yield Static("Uncommitted Changes", classes="pane-title")
                yield Static("File list here", id="uncommitted-files")

            # Middle pane
            with Vertical(classes="pane"):
                yield Static("Committed Files", classes="pane-title")
                yield Static("Commit list here", id="committed-files")

            # Right pane
            with Vertical(classes="pane"):
                yield Static("Active Changes", classes="pane-title")
                yield Static("Animations here", id="active-changes")

    yield Footer()
```

## CSS Styling

Textual uses CSS for styling:

```python
CSS = """
/* Make panes equal width */
.pane {
    width: 1fr;  /* Each pane takes equal space */
    height: 100%;
    border: solid $primary;  /* Border around each pane */
    margin: 1;  /* Space between panes */
    padding: 1;  /* Internal padding */
}

/* Style pane titles */
.pane-title {
    text-align: center;
    text-style: bold;
    margin-bottom: 1;
}

/* Scrollable file lists */
.file-list {
    height: 100%;
    overflow-y: auto;  /* Scroll when content overflows */
}
"""
```

## File Watching with inotify

Use `watchdog` for inotify file monitoring:

```python
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

class FileChangeHandler(FileSystemEventHandler):
    """Handle file system events."""

    def __init__(self, callback):
        self.callback = callback

    def on_modified(self, event):
        if not event.is_directory:
            self.callback('modified', event.src_path)

    def on_created(self, event):
        if not event.is_directory:
            self.callback('created', event.src_path)

class RepoWatchApp(App):
    # ... existing code ...

    def __init__(self, repo_path: Path):
        super().__init__()
        self.repo_path = repo_path
        self.observer = None

    async def start_file_watching(self):
        """Start inotify file watching."""
        self.observer = Observer()
        handler = FileChangeHandler(self.on_file_change)
        self.observer.schedule(handler, str(self.repo_path), recursive=True)
        self.observer.start()

    def on_file_change(self, event_type: str, file_path: str):
        """Handle file change events."""
        # Convert to relative path
        rel_path = Path(file_path).relative_to(self.repo_path)

        # Update UI
        asyncio.create_task(self.update_file_display(rel_path))

    async def update_file_display(self, changed_file: Path):
        """Update the file display."""
        # Get the uncommitted files widget
        files_widget = self.query_one("#uncommitted-files", Static)

        # Add this file to the display
        current_text = files_widget.renderable
        new_text = f"{current_text}\n📄 {changed_file}"

        files_widget.update(new_text)
```

## Git Integration

Use GitPython for git operations:

```python
import git
from git import Repo

class RepoWatchApp(App):
    # ... existing code ...

    def __init__(self, repo_path: Path):
        super().__init__()
        self.repo_path = repo_path
        self.repo = Repo(repo_path)

    def get_uncommitted_files(self):
        """Get list of uncommitted changed files."""
        # Get staged and unstaged files
        staged = [item.a_path for item in self.repo.index.diff('HEAD')]
        unstaged = [item.a_path for item in self.repo.index.diff(None)]

        # Combine and deduplicate
        all_files = list(set(staged + unstaged))
        return all_files

    def get_recent_commits(self, since_time):
        """Get commits since given time."""
        commits = list(self.repo.iter_commits(
            since=since_time,
            max_count=10
        ))

        committed_files = []
        for commit in commits:
            # Get files changed in this commit
            files = commit.stats.files.keys()
            committed_files.extend(files)

        return list(set(committed_files))  # Remove duplicates

    async def update_git_status(self):
        """Update git status display."""
        # Update uncommitted files
        uncommitted = self.get_uncommitted_files()
        uncommitted_widget = self.query_one("#uncommitted-files", Static)

        if uncommitted:
            file_list = "\n".join(f"📄 {file}" for file in uncommitted)
            uncommitted_widget.update(file_list)
        else:
            uncommitted_widget.update("No uncommitted changes")

        # Update committed files
        committed = self.get_recent_commits(self.session_start)
        committed_widget = self.query_one("#committed-files", Static)

        if committed:
            file_list = "\n".join(f"✅ {file}" for file in committed)
            committed_widget.update(file_list)
        else:
            committed_widget.update("No recent commits")
```

## Animation System

Animation for active changes that lasts 1 second per file:

```python
class RepoWatchApp(App):
    # ... existing code ...

    def __init__(self, repo_path: Path):
        super().__init__()
        self.repo_path = repo_path
        self.active_changes = {}  # file_path -> timestamp
        self.animation_frames = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
        self.frame_index = 0

    def on_file_change(self, event_type: str, file_path: str):
        """Handle file change events."""
        rel_path = Path(file_path).relative_to(self.repo_path)
        self.active_changes[rel_path] = datetime.now()

        # Update active changes display
        self.update_active_changes()

    def update_active_changes(self):
        """Update the active changes pane."""
        changes_widget = self.query_one("#active-changes", Static)

        if self.active_changes:
            spinner = self.animation_frames[self.frame_index % len(self.animation_frames)]
            file_list = "\n".join(f"{spinner} {file}" for file in self.active_changes.keys())
            changes_widget.update(f"Active changes:\n{file_list}")
        else:
            changes_widget.update("💤 Watching for changes...")

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

            await asyncio.sleep(0.1)  # 10 FPS
```

## Complete Working Example

Here's a complete, working Textual TUI for repoWatch:

```python
#!/usr/bin/env python3
"""
Working repoWatch Textual TUI
"""

import asyncio
import time
from datetime import datetime
from pathlib import Path

from textual.app import App, ComposeResult
from textual.containers import Container, Horizontal, Vertical
from textual.widgets import Static, Header, Footer

import git
from git import Repo
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

class FileChangeHandler(FileSystemEventHandler):
    """Handle file system events."""

    def __init__(self, callback):
        self.callback = callback

    def on_modified(self, event):
        if not event.is_directory and not event.src_path.endswith('.git'):
            self.callback('modified', event.src_path)

    def on_created(self, event):
        if not event.is_directory and not event.src_path.endswith('.git'):
            self.callback('created', event.src_path)

class RepoWatchApp(App):
    """Complete repoWatch Textual TUI."""

    CSS = """
    #main-content {
        height: 100%;
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

def main():
    """Main entry point."""
    import sys

    repo_path = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()

    app = RepoWatchApp(repo_path)
    app.run()

if __name__ == "__main__":
    main()
```

## Installation & Usage

1. **Install dependencies:**
   ```bash
   pip install textual gitpython watchdog
   ```

2. **Run the TUI:**
   ```bash
   python textual_repo_watch.py /path/to/git/repo
   ```

3. **What you'll see:**
   - **Left pane:** Files you've changed but not committed
   - **Middle pane:** Files committed since you started watching
   - **Right pane:** Active file changes with spinning animations
   - **Bottom:** Status bar with session info
   - **Footer:** Keybind reference (Tab/Shift+Tab navigation, Q/Ctrl+C quit)

## Key Textual Concepts Used

- **App class:** Main application container
- **compose() method:** Builds the UI layout
- **CSS styling:** Terminal styling system
- **query_one():** Find and update widgets
- **async event handling:** For file watching and updates
- **Container/Horizontal/Vertical:** Layout management

## Why This Works

1. **Simple structure:** Clear separation of concerns
2. **Proper Textual usage:** Uses containers, widgets, and CSS correctly
3. **Real functionality:** Actually displays file changes
4. **Maintainable:** Easy to understand and modify

This is how you build a working Textual TUI for repoWatch. Focus on getting content to display first, then add complexity later.
