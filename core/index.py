#!/usr/bin/env python3
"""
repoWatch Core Module

Entry point for the core TUI module following the Matryoshka pattern.
"""

import sys
from pathlib import Path
from typing import Dict, Any, Optional

# Add parent directory to ensure we import our modules, not system ones
_repo_watch_root = Path(__file__).parent.parent
if str(_repo_watch_root) not in sys.path:
    sys.path.insert(0, str(_repo_watch_root))

from .orchestrator import CoreOrchestrator


def main(module_path: Path, parent_config: Optional[Dict[str, Any]] = None) -> bool:
    """Entry point for the core module."""
    try:
        orchestrator = CoreOrchestrator(module_path, parent_config)

        # Get and expand settings from parent config
        paths_config = parent_config.get("paths", {})
        repo_path_str = orchestrator._expandvars(paths_config.get("repo_path", "."))
        refresh_interval_str = orchestrator._expandvars(paths_config.get("refresh_interval", "1.0"))

        repo_path = Path(repo_path_str).resolve()
        refresh_interval = float(refresh_interval_str)

        print(f"Starting repoWatch TUI for repository: {repo_path}")
        print("Press 'q' to quit gracefully, Ctrl+C to force quit immediately")

        # Run the application
        result = orchestrator.run_app(repo_path, refresh_interval)

        # If TUI exited (either via quit or other means), exit the entire process
        print("TUI closed, exiting process...")
        sys.exit(0)

    except Exception as e:
        print(f"Core module error: {e}")
        import traceback
        traceback.print_exc()
        return False
