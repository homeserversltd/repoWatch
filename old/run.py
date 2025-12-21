#!/usr/bin/env python3
"""
Simple runner for repoWatch
"""

import os
import sys
from pathlib import Path

def main():
    """Run repoWatch."""
    # Set repo path from command line, environment, or use current directory
    if len(sys.argv) > 1:
        repo_path = sys.argv[1]
    else:
        repo_path = os.environ.get('REPO_WATCH_REPO_PATH', '.')
    os.environ['REPO_WATCH_REPO_PATH'] = repo_path

    print(f"repoWatch - Starting simple Textual TUI")
    print(f"Watching repository: {repo_path}")
    print("Use Tab/Shift+Tab to navigate panes. Press Q or Ctrl+C to exit.")
    print("Keybinds are also displayed in the footer at the bottom of the TUI.")

    # Import and run the app
    sys.path.insert(0, str(Path(__file__).parent))
    import index

    success = index.main()
    return success

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)