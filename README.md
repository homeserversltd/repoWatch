# repoWatch - Simple Textual TUI

A minimal Textual-based terminal UI for monitoring git repository changes in real-time.

## Features

- **File Watching**: Uses inotify to detect file changes instantly
- **Git Integration**: Shows uncommitted changes and recent commits
- **Live Animations**: Visual indicators for active file modifications
- **Three-Pane Layout**: Clean separation of different file states

## Installation

```bash
# Install dependencies
pip install textual watchdog gitpython

# Or run directly (will auto-install dependencies)
python run.py
```

## Usage

Use the provided runner script for proper environment setup and argument handling:

```bash
# Watch current directory
./run.sh

# Watch specific repository
./run.sh --repo /path/to/git/repo

# Show help
./run.sh --help
```

The runner script automatically sets the `REPO_WATCH_REPO_PATH` environment variable and provides helpful usage information before launching the TUI.

## Interface

The TUI displays three columns:

### Left: Uncommitted Changes
Shows files you've modified but not committed:
- 📄 filename (modified/untracked files)
- Updates automatically when files change

### Middle: Committed Files
Shows files from recent commits in this session:
- ✅ filename (files in recent commits)
- Limited to last 10 commits since session start

### Right: Active Changes
Shows files currently being modified:
- ⠋ filename (animated spinner + filename, lasts 1 second per file)
- 💤 Watching... (when no active changes)

### Bottom: Status Bar
Shows session statistics:
- Session duration
- Count of uncommitted files
- Count of committed files

### Footer: Keybind Reference
Shows available keyboard shortcuts:
- Tab/Shift+Tab: Navigate between panes
- Q/Escape/Ctrl+C: Quit the application

## Controls

### Navigation
- **Tab**: Navigate between panes
- **Shift+Tab**: Navigate backwards

### Exiting the Application
- **Ctrl+Q**: Default Textual exit (built-in)
- **Q**: Quit the app
- **Escape**: Quit the app
- **Ctrl+C**: Quit the app (force quit)

## Architecture

Built with pure Textual following these principles:

- **Simple**: No complex abstractions or orchestrators
- **Direct**: App class → compose() → widgets → event handlers
- **Working**: Actually displays content (unlike previous versions)
- **Minimal**: Only essential features for file watching

## Files

- `index.py` - Main Textual application
- `file_filter.py` - File system event filtering and processing
- `index.json` - Configuration (dependencies, paths)
- `run.py` - Simple runner script
- `textual.md` - Implementation documentation
- `reference/` - Previous complex implementations (kept for reference)

## Dependencies

- **textual**: Terminal UI framework
- **watchdog**: File system monitoring (inotify)
- **gitpython**: Git repository access

Dependencies are automatically installed on first run.