#!/bin/bash
# repoWatch - Git Repository Watcher TUI
#
# Launch the repoWatch TUI with configurable options
#

# Change to script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Check if dependencies are installed system-wide
if ! python3 -c "import textual" 2>/dev/null; then
    echo "Installing dependencies system-wide..."
    pip install --break-system-packages -r requirements.txt
fi

# Parse command line arguments
REPO_PATH="."
REFRESH_INTERVAL=1
THEME="dark"
CONFIG_FILE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --repo)
            REPO_PATH="$2"
            shift 2
            ;;
        --refresh)
            REFRESH_INTERVAL="$2"
            shift 2
            ;;
        --theme)
            THEME="$2"
            shift 2
            ;;
        --config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        --help)
            echo "repoWatch - Git Repository Watcher TUI"
            echo ""
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --repo PATH       Git repository to monitor (default: current directory)"
            echo "  --refresh SEC     File system polling interval in seconds (default: 1)"
            echo "  --theme THEME     Color theme (dark/light) (default: dark)"
            echo "  --config FILE     Custom configuration file"
            echo "  --help           Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                                # Watch current directory"
            echo "  $0 --repo ~/projects/my-app      # Watch specific repo"
            echo "  $0 --refresh 2                   # Slower polling for performance"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Validate repository path
if [ ! -d "$REPO_PATH/.git" ]; then
    echo "Error: $REPO_PATH is not a git repository"
    echo "Please specify a valid git repository with --repo"
    exit 1
fi

# Export environment variables for the Python app
export REPO_WATCH_REPO_PATH="$REPO_PATH"
export REPO_WATCH_REFRESH_INTERVAL="$REFRESH_INTERVAL"
export REPO_WATCH_THEME="$THEME"
export REPO_WATCH_CONFIG_FILE="$CONFIG_FILE"

# Launch the TUI application
echo "Starting repoWatch..."
echo "Repository: $REPO_PATH"
echo "Theme: $THEME"
echo "Refresh: ${REFRESH_INTERVAL}s"
echo "Press Ctrl+C to exit"
echo ""

python3 repo_watch.py
