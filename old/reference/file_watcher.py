"""
repoWatch File Watching System

Uses watchdog to monitor file system changes in real-time.
"""

import os
import time
from pathlib import Path
from typing import Callable, List, Set, Dict, Any, Optional
from dataclasses import dataclass

try:
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler, FileSystemEvent
    WATCHDOG_AVAILABLE = True
except ImportError:
    WATCHDOG_AVAILABLE = False


@dataclass
class FileChangeEvent:
    """Represents a file system change event."""
    event_type: str  # 'created', 'modified', 'deleted', 'moved'
    file_path: str
    is_directory: bool
    timestamp: float

    @property
    def relative_path(self) -> str:
        """Get the relative path from the watch root."""
        return self.file_path


class FileChangeHandler(FileSystemEventHandler):
    """Watchdog event handler for file changes."""

    def __init__(self, callback: Callable[[FileChangeEvent], None],
                 ignore_patterns: Optional[List[str]] = None):
        self.callback = callback
        self.ignore_patterns = ignore_patterns or []
        self.last_events: Dict[str, float] = {}  # Prevent duplicate events

    def _should_ignore(self, path: str) -> bool:
        """Check if path should be ignored."""
        # Ignore common patterns
        ignore_basenames = {
            '.git', '__pycache__', '.DS_Store', 'Thumbs.db',
            '.swp', '.swo', '~', '.tmp', '.bak'
        }

        basename = os.path.basename(path)
        if basename in ignore_basenames:
            return True

        # Check custom ignore patterns
        for pattern in self.ignore_patterns:
            if pattern in path:
                return True

        return False

    def _is_duplicate_event(self, path: str, event_time: float) -> bool:
        """Check if this is a duplicate event (too close to previous)."""
        last_time = self.last_events.get(path, 0)
        if event_time - last_time < 0.1:  # 100ms threshold
            return True

        self.last_events[path] = event_time
        return False

    def _handle_event(self, event: FileSystemEvent, event_type: str):
        """Handle a file system event."""
        if self._should_ignore(event.src_path):
            return

        event_time = time.time()
        if self._is_duplicate_event(event.src_path, event_time):
            return

        change_event = FileChangeEvent(
            event_type=event_type,
            file_path=event.src_path,
            is_directory=event.is_directory,
            timestamp=event_time
        )

        self.callback(change_event)

    def on_created(self, event: FileSystemEvent):
        """Handle file/directory creation."""
        self._handle_event(event, 'created')

    def on_modified(self, event: FileSystemEvent):
        """Handle file/directory modification."""
        # Skip directory modifications (only care about file content changes)
        if not event.is_directory:
            self._handle_event(event, 'modified')

    def on_deleted(self, event: FileSystemEvent):
        """Handle file/directory deletion."""
        self._handle_event(event, 'deleted')

    def on_moved(self, event: FileSystemEvent):
        """Handle file/directory move/rename."""
        self._handle_event(event, 'moved')


class FileWatcher:
    """Manages file system monitoring using watchdog."""

    def __init__(self, watch_path: Path, callback: Callable[[FileChangeEvent], None],
                 ignore_patterns: Optional[List[str]] = None):
        self.watch_path = watch_path.resolve()
        self.callback = callback
        self.ignore_patterns = ignore_patterns or []

        self.observer: Optional[Observer] = None
        self.handler: Optional[FileChangeHandler] = None
        self.is_watching = False

        if not WATCHDOG_AVAILABLE:
            print("Warning: watchdog not available. File watching will be disabled.")
            print("Install with: pip install watchdog")

    def start(self) -> bool:
        """
        Start watching the directory.

        Returns:
            True if watching started successfully
        """
        if not WATCHDOG_AVAILABLE:
            return False

        if self.is_watching:
            return True

        try:
            self.handler = FileChangeHandler(self.callback, self.ignore_patterns)
            self.observer = Observer()
            self.observer.schedule(self.handler, str(self.watch_path), recursive=True)
            self.observer.start()
            self.is_watching = True
            return True

        except Exception as e:
            print(f"Error starting file watcher: {e}")
            return False

    def stop(self):
        """Stop watching."""
        if self.observer and self.is_watching:
            self.observer.stop()
            self.observer.join(timeout=1.0)
            self.is_watching = False

    def is_active(self) -> bool:
        """Check if the watcher is active."""
        return self.is_watching and self.observer and self.observer.is_alive()

    def get_watch_path(self) -> Path:
        """Get the path being watched."""
        return self.watch_path

    def set_ignore_patterns(self, patterns: List[str]):
        """Update ignore patterns."""
        self.ignore_patterns = patterns
        if self.handler:
            self.handler.ignore_patterns = patterns


class MockFileWatcher(FileWatcher):
    """Mock file watcher for testing when watchdog is not available."""

    def __init__(self, watch_path: Path, callback: Callable[[FileChangeEvent], None],
                 ignore_patterns: Optional[List[str]] = None):
        super().__init__(watch_path, callback, ignore_patterns)
        self.mock_events: List[FileChangeEvent] = []

    def start(self) -> bool:
        """Mock start - always succeeds."""
        self.is_watching = True
        return True

    def stop(self):
        """Mock stop."""
        self.is_watching = False

    def simulate_file_change(self, file_path: str, event_type: str = 'modified'):
        """Simulate a file change event."""
        if self.callback:
            event = FileChangeEvent(
                event_type=event_type,
                file_path=str(self.watch_path / file_path),
                is_directory=False,
                timestamp=time.time()
            )
            self.callback(event)


def create_file_watcher(watch_path: Path, callback: Callable[[FileChangeEvent], None],
                       ignore_patterns: Optional[List[str]] = None) -> FileWatcher:
    """
    Create an appropriate file watcher based on available libraries.

    Args:
        watch_path: Directory to watch
        callback: Function to call when files change
        ignore_patterns: Patterns to ignore

    Returns:
        FileWatcher instance
    """
    if WATCHDOG_AVAILABLE:
        return FileWatcher(watch_path, callback, ignore_patterns)
    else:
        print("Using mock file watcher (watchdog not available)")
        return MockFileWatcher(watch_path, callback, ignore_patterns)

