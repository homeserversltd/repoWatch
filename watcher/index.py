#!/usr/bin/env python3
"""
repoWatch Watcher Module

Real-time file system monitoring using watchdog.
"""

import os
import time
from pathlib import Path
from typing import Callable, List, Set, Dict, Any, Optional
from dataclasses import dataclass
import json
import sys

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
                 ignore_patterns: Optional[List[str]] = None,
                 config: Optional[Dict[str, Any]] = None):
        self.callback = callback
        self.ignore_patterns = ignore_patterns or []
        self.config = config or {}
        self.last_events: Dict[str, float] = {}  # Prevent duplicate events
        self.watcher_config = self.config.get("config", {}).get("watcher_settings", {})

        # Get debounce settings
        self.debounce_delay = self.watcher_config.get("debounce_delay", 0.5)
        self.max_events_per_second = self.watcher_config.get("max_events_per_second", 10)
        self.event_buffer_size = self.watcher_config.get("event_buffer_size", 100)

        # Rate limiting
        self.event_times: List[float] = []
        self.event_count = 0

    def _should_ignore(self, path: str) -> bool:
        """Check if path should be ignored."""
        ignore_config = self.config.get("config", {}).get("ignore_patterns", {})

        # Check directory patterns
        for dir_pattern in ignore_config.get("directories", []):
            if dir_pattern in path:
                return True

        # Check file patterns
        for file_pattern in ignore_config.get("files", []):
            if path.endswith(file_pattern):
                return True

        # Check custom ignore patterns
        for pattern in self.ignore_patterns:
            if pattern in path:
                return True

        # Ignore common patterns
        ignore_basenames = {
            '.git', '__pycache__', '.DS_Store', 'Thumbs.db',
            '.swp', '.swo', '~', '.tmp', '.bak'
        }

        basename = os.path.basename(path)
        if basename in ignore_basenames:
            return True

        return False

    def _check_rate_limit(self) -> bool:
        """Check if we're within rate limits."""
        now = time.time()

        # Clean old events
        cutoff = now - 1.0  # 1 second window
        self.event_times = [t for t in self.event_times if t > cutoff]

        # Check events per second
        if len(self.event_times) >= self.max_events_per_second:
            return False

        # Add current event
        self.event_times.append(now)
        return True

    def _debounce_event(self, event_key: str) -> bool:
        """Check if event should be debounced."""
        now = time.time()
        last_time = self.last_events.get(event_key, 0)

        if now - last_time < self.debounce_delay:
            return False  # Debounce

        self.last_events[event_key] = now
        return True

    def _create_event(self, event: FileSystemEvent) -> FileChangeEvent:
        """Create a FileChangeEvent from a watchdog event."""
        # Map watchdog event types to our types
        event_type_map = {
            'created': 'created',
            'modified': 'modified',
            'deleted': 'deleted',
            'moved': 'moved'
        }

        event_type = event_type_map.get(event.event_type, event.event_type)

        return FileChangeEvent(
            event_type=event_type,
            file_path=event.src_path,
            is_directory=event.is_directory,
            timestamp=time.time()
        )

    def on_any_event(self, event: FileSystemEvent):
        """Handle any file system event."""
        # Skip if ignored
        if self._should_ignore(event.src_path):
            return

        # Rate limiting
        if not self._check_rate_limit():
            return

        # Debouncing
        event_key = f"{event.event_type}:{event.src_path}"
        if not self._debounce_event(event_key):
            return

        # Create and dispatch event
        change_event = self._create_event(event)
        try:
            self.callback(change_event)
        except Exception as e:
            print(f"Error in file change callback: {e}")

    def on_created(self, event: FileSystemEvent):
        """Handle file creation."""
        self.on_any_event(event)

    def on_modified(self, event: FileSystemEvent):
        """Handle file modification."""
        self.on_any_event(event)

    def on_deleted(self, event: FileSystemEvent):
        """Handle file deletion."""
        self.on_any_event(event)

    def on_moved(self, event: FileSystemEvent):
        """Handle file move/rename."""
        self.on_any_event(event)


class PollingFileWatcher:
    """Fallback file watcher using polling when watchdog is not available."""

    def __init__(self, watch_path: Path, callback: Callable[[FileChangeEvent], None],
                 ignore_patterns: Optional[List[str]] = None,
                 config: Optional[Dict[str, Any]] = None):
        self.watch_path = watch_path
        self.callback = callback
        self.ignore_patterns = ignore_patterns or []
        self.config = config or {}
        self.fallback_config = self.config.get("config", {}).get("fallback_mode", {})
        self.is_active = False
        self.polling_interval = self.fallback_config.get("polling_interval", 1.0)

        # Track file states
        self.last_mtimes: Dict[str, float] = {}
        self.known_files: Set[str] = set()

        # Scan initial state
        self._scan_initial_state()

    def _scan_initial_state(self):
        """Scan the initial state of files."""
        self.last_mtimes = {}
        self.known_files = set()

        for root, dirs, files in os.walk(self.watch_path):
            # Filter ignored directories
            dirs[:] = [d for d in dirs if not self._should_ignore_dir(os.path.join(root, d))]

            for file in files:
                file_path = os.path.join(root, file)
                if self._should_ignore_file(file_path):
                    continue

                try:
                    mtime = os.path.getmtime(file_path)
                    self.last_mtimes[file_path] = mtime
                    self.known_files.add(file_path)
                except OSError:
                    continue

    def _should_ignore_dir(self, path: str) -> bool:
        """Check if directory should be ignored."""
        ignore_config = self.config.get("config", {}).get("ignore_patterns", {})

        for dir_pattern in ignore_config.get("directories", []):
            if dir_pattern in path:
                return True

        return False

    def _should_ignore_file(self, path: str) -> bool:
        """Check if file should be ignored."""
        ignore_config = self.config.get("config", {}).get("ignore_patterns", {})

        # Check directory patterns in path
        for dir_pattern in ignore_config.get("directories", []):
            if dir_pattern in path:
                return True

        # Check file patterns
        for file_pattern in ignore_config.get("files", []):
            if path.endswith(file_pattern):
                return True

        # Check custom ignore patterns
        for pattern in self.ignore_patterns:
            if pattern in path:
                return True

        return False

    def _check_for_changes(self):
        """Check for file system changes."""
        current_files = set()
        new_mtimes = {}

        for root, dirs, files in os.walk(self.watch_path):
            # Filter ignored directories
            dirs[:] = [d for d in dirs if not self._should_ignore_dir(os.path.join(root, d))]

            for file in files:
                file_path = os.path.join(root, file)
                if self._should_ignore_file(file_path):
                    continue

                current_files.add(file_path)

                try:
                    mtime = os.path.getmtime(file_path)
                    new_mtimes[file_path] = mtime

                    # Check for modifications
                    if file_path in self.last_mtimes:
                        if mtime > self.last_mtimes[file_path]:
                            event = FileChangeEvent(
                                event_type='modified',
                                file_path=file_path,
                                is_directory=False,
                                timestamp=time.time()
                            )
                            self.callback(event)
                    else:
                        # New file
                        event = FileChangeEvent(
                            event_type='created',
                            file_path=file_path,
                            is_directory=False,
                            timestamp=time.time()
                        )
                        self.callback(event)

                except OSError:
                    continue

        # Check for deletions
        deleted_files = self.known_files - current_files
        for file_path in deleted_files:
            event = FileChangeEvent(
                event_type='deleted',
                file_path=file_path,
                is_directory=False,
                timestamp=time.time()
            )
            self.callback(event)

        # Update state
        self.last_mtimes = new_mtimes
        self.known_files = current_files

    def start(self) -> bool:
        """Start the file watcher."""
        self.is_active = True
        return True

    def stop(self):
        """Stop the file watcher."""
        self.is_active = False

    def is_active(self) -> bool:
        """Check if the watcher is active."""
        return self.is_active


class WatchdogFileWatcher:
    """File watcher using watchdog library."""

    def __init__(self, watch_path: Path, callback: Callable[[FileChangeEvent], None],
                 ignore_patterns: Optional[List[str]] = None,
                 config: Optional[Dict[str, Any]] = None):
        self.watch_path = watch_path
        self.callback = callback
        self.ignore_patterns = ignore_patterns or []
        self.config = config or {}

        self.observer: Optional[Observer] = None
        self.handler: Optional[FileChangeHandler] = None
        self.is_active_flag = False

    def start(self) -> bool:
        """Start the file watcher."""
        if not WATCHDOG_AVAILABLE:
            return False

        try:
            self.handler = FileChangeHandler(
                self.callback,
                self.ignore_patterns,
                self.config
            )

            self.observer = Observer()
            recursive = self.config.get("config", {}).get("watcher_settings", {}).get("recursive_watch", True)
            self.observer.schedule(self.handler, str(self.watch_path), recursive=recursive)

            self.observer.start()
            self.is_active_flag = True
            return True

        except Exception as e:
            print(f"Failed to start watchdog watcher: {e}")
            return False

    def stop(self):
        """Stop the file watcher."""
        if self.observer:
            try:
                self.observer.stop()
                self.observer.join(timeout=1.0)
            except Exception as e:
                print(f"Error stopping watcher: {e}")

        self.is_active_flag = False

    def is_active(self) -> bool:
        """Check if the watcher is active."""
        return self.is_active_flag


def create_file_watcher(watch_path: Path, callback: Callable[[FileChangeEvent], None],
                       ignore_patterns: Optional[List[str]] = None,
                       config: Optional[Dict[str, Any]] = None) -> Optional[Any]:
    """Create a file watcher instance."""
    config = config or {}

    # Try watchdog first
    if WATCHDOG_AVAILABLE and not config.get("config", {}).get("fallback_mode", {}).get("use_polling", False):
        watcher = WatchdogFileWatcher(watch_path, callback, ignore_patterns, config)
        if watcher.start():
            return watcher

    # Fallback to polling
    print("Using polling file watcher (watchdog not available or failed)")
    watcher = PollingFileWatcher(watch_path, callback, ignore_patterns, config)
    watcher.start()
    return watcher


class WatcherOrchestrator:
    """Orchestrator for the file watching module."""

    def __init__(self, module_path: Path, parent_config: Optional[Dict[str, Any]] = None):
        self.module_path = module_path
        self.parent_config = parent_config or {}
        self.config_path = module_path / "index.json"
        self.config = self._load_config()

    def _load_config(self) -> Dict[str, Any]:
        """Load configuration from index.json."""
        try:
            with open(self.config_path, 'r') as f:
                return json.load(f)
        except FileNotFoundError:
            print(f"Warning: Watcher config not found: {self.config_path}")
            return {}
        except json.JSONDecodeError as e:
            print(f"Warning: Invalid watcher config JSON: {e}")
            return {}


def main(module_path: Path, parent_config: Optional[Dict[str, Any]] = None) -> bool:
    """Entry point for the watcher module."""
    try:
        orchestrator = WatcherOrchestrator(module_path, parent_config)

        # This module is primarily a library - no standalone execution needed
        # The core module will import and use the file watching classes
        print("Watcher module loaded successfully")
        return True

    except Exception as e:
        print(f"Watcher module error: {e}")
        import traceback
        traceback.print_exc()
        return False


# Export the main classes for import by other modules
__all__ = ['create_file_watcher', 'FileChangeEvent', 'WatchdogFileWatcher', 'PollingFileWatcher']
