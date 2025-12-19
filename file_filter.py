#!/usr/bin/env python3
"""
File Filtering Component for repoWatch

Handles file system event filtering and processing.
Filters out directories and git-related files, passes valid file events to callback.
"""

from watchdog.events import FileSystemEventHandler


class FileChangeHandler(FileSystemEventHandler):
    """Handle file system events with filtering."""

    def __init__(self, callback):
        """Initialize with callback function.

        Args:
            callback: Function to call with (event_type, file_path) for valid events
        """
        self.callback = callback

    def on_modified(self, event):
        """Handle file modification events."""
        if self._is_valid_file_event(event):
            self.callback('modified', event.src_path)

    def on_created(self, event):
        """Handle file creation events."""
        if self._is_valid_file_event(event):
            self.callback('created', event.src_path)

    def on_deleted(self, event):
        """Handle file deletion events."""
        if self._is_valid_file_event(event):
            self.callback('deleted', event.src_path)

    def on_moved(self, event):
        """Handle file move events."""
        if self._is_valid_file_event(event):
            self.callback('moved', event.dest_path)

    def _is_valid_file_event(self, event):
        """Filter events to only include valid files.

        Returns:
            bool: True if event should be processed
        """
        # Skip directories
        if event.is_directory:
            return False

        # Skip git-related files
        if event.src_path.endswith('.git'):
            return False

        # Skip temporary files (common patterns)
        if any(pattern in event.src_path for pattern in [
            '.tmp', '.swp', '.swo', '~', '.bak'
        ]):
            return False

        return True

