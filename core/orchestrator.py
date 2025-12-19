#!/usr/bin/env python3
"""
repoWatch Core Orchestrator

Orchestrator for the core TUI module following the Matryoshka pattern.
"""

import json
import os
import signal
import sys
from pathlib import Path
from typing import Dict, Any, Optional

from .app import RepoWatchApp


class CoreOrchestrator:
    """Orchestrator for the core TUI module."""

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
            print(f"Warning: Core config not found: {self.config_path}")
            return {}
        except json.JSONDecodeError as e:
            print(f"Warning: Invalid core config JSON: {e}")
            return {}

    def _expandvars(self, path: str) -> str:
        """Custom variable expansion that handles ${VAR:-default} syntax."""
        import re

        # Handle ${VAR:-default} syntax
        def replace_var(match):
            var_expr = match.group(1)
            if ':-' in var_expr:
                var_name, default = var_expr.split(':-', 1)
                return os.environ.get(var_name, default)
            else:
                return os.environ.get(var_expr, match.group(0))

        # Replace ${VAR} and ${VAR:-default} patterns
        expanded = re.sub(r'\$\{([^}]+)\}', replace_var, path)

        # Also handle $VAR syntax for compatibility
        expanded = os.path.expandvars(expanded)

        return expanded

    def run_app(self, repo_path: Path, refresh_interval: float = 1.0):
        """Run the main repoWatch TUI application."""
        app = RepoWatchApp(repo_path, refresh_interval)

        # Set up signal handler that works with Textual
        # Textual's event loop should allow signals to propagate
        def signal_handler(signum, frame):
            print("\nSIGINT (Ctrl+C) received - exiting...", flush=True)
            # Try to exit gracefully through the app
            try:
                if app.file_watcher:
                    app.file_watcher.stop()
            except Exception:
                pass
            # Force exit if graceful exit doesn't work
            os._exit(0)

        # Register signal handler before running app
        signal.signal(signal.SIGINT, signal_handler)

        try:
            result = app.run()
            print(f"TUI exited normally, terminating process...", flush=True)
            sys.exit(0)
        except KeyboardInterrupt:
            print("KeyboardInterrupt caught - force exiting...", flush=True)
            os._exit(1)
        except Exception as e:
            print(f"Error running app: {e}", flush=True)
            os._exit(1)
