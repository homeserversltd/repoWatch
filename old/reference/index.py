#!/usr/bin/env python3
"""
repoWatch Root Orchestrator

InfiniteIndex orchestrator for the repoWatch system.
Loads configuration and executes child modules.
"""

import os
import sys
import json
import subprocess
from pathlib import Path
from typing import Dict, Any, Optional


class RepoWatchOrchestrator:
    """Root orchestrator for the repoWatch infiniteIndex system."""

    def __init__(self, module_path: Optional[Path] = None):
        self.module_path = module_path or Path(__file__).parent
        self.config_path = self.module_path / "index.json"
        self.config = self._load_config()
        self.paths = self._resolve_paths()

    def _load_config(self) -> Dict[str, Any]:
        """Load configuration from index.json."""
        try:
            with open(self.config_path, 'r') as f:
                return json.load(f)
        except FileNotFoundError:
            print(f"Error: Configuration file not found: {self.config_path}")
            sys.exit(1)
        except json.JSONDecodeError as e:
            print(f"Error: Invalid JSON in configuration file: {e}")
            sys.exit(1)

    def _resolve_paths(self) -> Dict[str, str]:
        """Resolve paths with environment variable expansion."""
        paths_config = self.config.get("paths", {})
        resolved = {}
        for key, value in paths_config.items():
            resolved[key] = self._expandvars(str(value))
        return resolved

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

    def _check_dependencies(self) -> bool:
        """Check if required dependencies are installed."""
        dependencies = self.config.get("config", {}).get("dependencies", {})

        missing_deps = []
        for dep, version in dependencies.items():
            try:
                # Handle module name conversion (e.g., gitpython -> git)
                module_name = dep.replace("-", "_")
                if dep == "gitpython":
                    module_name = "git"
                __import__(module_name)
            except ImportError:
                missing_deps.append(f"{dep}{version}")

        if missing_deps:
            print("Missing dependencies. Installing...")
            try:
                for dep_spec in missing_deps:
                    print(f"Installing {dep_spec}...")
                    subprocess.check_call([
                        sys.executable, "-m", "pip", "install",
                        "--break-system-packages", dep_spec
                    ])
                print("Dependencies installed successfully.")
                return True
            except subprocess.CalledProcessError as e:
                print(f"Failed to install dependencies: {e}")
                return False

        return True

    def _execute_child(self, child_path: Path, child_name: str) -> bool:
        """Execute a child module."""
        child_index_py = child_path / "index.py"

        if not child_index_py.exists():
            print(f"Warning: Child module '{child_name}' not found at {child_index_py}")
            return False

        try:
            print(f"Executing child module: {child_name}")
            # Temporarily modify sys.path to prioritize our local modules
            original_path = sys.path[:]
            # Insert the child path at the beginning, but keep all other paths
            sys.path.insert(0, str(child_path))
            try:
                import index as child_module
                # Call main function if it exists, otherwise just import
                if hasattr(child_module, 'main'):
                    result = child_module.main(child_path, self.config)
                    return result is not False
                else:
                    return True
            finally:
                # Remove only our inserted path
                if sys.path[0] == str(child_path):
                    sys.path.pop(0)
                # Restore original order for any other modifications
                sys.path = original_path

        except Exception as e:
            print(f"Error executing child module '{child_name}': {e}")
            execution_config = self.config.get("execution", {})
            if not execution_config.get("continue_on_error", True):
                return False
            return True

    def execute(self) -> bool:
        """Execute all child modules according to configuration."""
        print("repoWatch - Starting infiniteIndex orchestrator")

        # Check dependencies first
        if not self._check_dependencies():
            print("Failed to satisfy dependencies")
            return False

        # Get execution configuration
        execution_config = self.config.get("execution", {})
        children = self.config.get("children", [])

        success = True

        for child_name in children:
            child_path = self.module_path / child_name

            if not child_path.exists():
                print(f"Warning: Child directory '{child_name}' not found")
                if not execution_config.get("continue_on_error", True):
                    success = False
                    break
                continue

            child_success = self._execute_child(child_path, child_name)
            if not child_success and not execution_config.get("continue_on_error", True):
                success = False
                break

        if success:
            print("repoWatch orchestrator completed successfully")
        else:
            print("repoWatch orchestrator completed with errors")

        return success

    def get_config(self) -> Dict[str, Any]:
        """Get the loaded configuration."""
        return self.config

    def get_paths(self) -> Dict[str, str]:
        """Get the resolved paths."""
        return self.paths


def main(module_path: Optional[Path] = None, parent_config: Optional[Dict[str, Any]] = None) -> bool:
    """Entry point for the repoWatch orchestrator."""
    try:
        orchestrator = RepoWatchOrchestrator(module_path)
        return orchestrator.execute()
    except KeyboardInterrupt:
        print("\nrepoWatch orchestrator interrupted by user")
        return False
    except Exception as e:
        print(f"repoWatch orchestrator error: {e}")
        import traceback
        traceback.print_exc()
        return False


if __name__ == "__main__":
    # When run directly, execute the orchestrator
    success = main()
    sys.exit(0 if success else 1)