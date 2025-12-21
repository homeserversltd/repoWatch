#!/usr/bin/env python3
"""
Simple runner for repoWatch
"""

import os
import sys
from pathlib import Path

# Add current directory to path so we can import index
sys.path.insert(0, str(Path(__file__).parent))

import index

def main():
    """Run repoWatch."""
    repo_path = os.environ.get('REPO_WATCH_REPO_PATH', '.')

    print(f"repoWatch - Starting simple Textual TUI")
    print(f"Watching repository: {repo_path}")
    print("Press Ctrl+C to exit")

    success = index.main()
    return success

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)