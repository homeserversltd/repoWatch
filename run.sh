#!/bin/bash
#
# repoWatch runner script
# Usage: ./run.sh [--repo /path/to/repository]
#

# Default repository path (current directory)
REPO_PATH="."

# Parse command line arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --repo)
      REPO_PATH="$2"
      shift 2
      ;;
    -h|--help)
      echo "Usage: $0 [--repo /path/to/repository]"
      echo ""
      echo "Options:"
      echo "  --repo PATH    Path to git repository to watch (default: current directory)"
      echo "  -h, --help     Show this help message"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Use --help for usage information"
      exit 1
      ;;
  esac
done

# Set environment variable for the Python script
export REPO_WATCH_REPO_PATH="$REPO_PATH"

# Run the repoWatch TUI
echo "repoWatch - Starting Textual TUI"
echo "Watching repository: $REPO_PATH"
echo "Press Ctrl+C to exit"
echo ""

python3 run.py