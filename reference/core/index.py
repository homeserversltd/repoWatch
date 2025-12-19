#!/usr/bin/env python3
"""
repoWatch Core Module

Entry point for the core TUI module following the Matryoshka pattern.
"""

import sys
from pathlib import Path
from typing import Dict, Any, Optional

# Import orchestrator directly since it's in the same directory
import orchestrator as core_orchestrator


def main(module_path: Path, parent_config: Optional[Dict[str, Any]] = None) -> bool:
    """Entry point for the core module."""
    try:
        orchestrator = core_orchestrator.CoreOrchestrator(module_path, parent_config)

        # Get and expand settings from parent config
        paths_config = parent_config.get("paths", {})
        repo_path_str = orchestrator._expandvars(paths_config.get("repo_path", "."))
        refresh_interval_str = orchestrator._expandvars(paths_config.get("refresh_interval", "1.0"))

        repo_path = Path(repo_path_str).resolve()
        refresh_interval = float(refresh_interval_str)

        print(f"Starting repoWatch TUI for repository: {repo_path}")
        print("Press 'q', 'ESC', or 'Ctrl+C' to quit")

        # Run the application - Textual handles exit internally
        orchestrator.run_app(repo_path, refresh_interval)

    except Exception as e:
        print(f"Core module error: {e}")
        import traceback
        traceback.print_exc()
        return False