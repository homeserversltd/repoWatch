#!/usr/bin/env python3
"""
repoWatch Core Orchestrator

Orchestrator for the core TUI module following the Matryoshka pattern.
"""

import json
import os
import re
from pathlib import Path
from typing import Dict, Any, Optional

import app as app_module


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
        app = app_module.RepoWatchApp(repo_path, refresh_interval)
        
        # Run the app - Textual handles exit internally via self.exit()
        # When user presses q, escape, or ctrl+c, the app will exit gracefully
        app.run()
