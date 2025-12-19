#!/usr/bin/env python3
"""
Test script to debug repoWatch file display issue
"""

import sys
import os
from pathlib import Path

# Add repoWatch to path
sys.path.insert(0, str(Path(__file__).parent))

from core.index import RepoWatchApp
from display.index import display_all_files_flat

def test_file_display():
    """Test the file display functionality."""
    print("=== Testing File Display ===")

    # Create a test repo path
    repo_path = Path(".")

    # Test the display function directly
    test_files = ["test1.py", "test2.py", "subdir/test3.py"]

    print(f"Input files: {test_files}")
    result = display_all_files_flat(test_files, repo_path)
    print(f"Display result:\n{result}")
    print(f"Result type: {type(result)}")
    print(f"Result length: {len(result)}")

    # Test with empty list
    print("\n=== Testing Empty File List ===")
    empty_result = display_all_files_flat([], repo_path)
    print(f"Empty result: '{empty_result}'")

    print("\n=== File Display Test Complete ===")

if __name__ == "__main__":
    test_file_display()
