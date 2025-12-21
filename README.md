# repoWatch Components Reference

**repoWatch** is a modular git repository monitoring system built with C components using the infinite index pattern. Each component is a standalone executable that communicates via JSON reports.

## Core Components

### git-submodules
**Input:** Repository path, max depth, check interval  
**Output:** `git-submodules.report` (JSON) - Status of all repositories and submodules  
**Purpose:** Recursively scans git repositories and reports which ones have uncommitted changes

### committed-not-pushed
**Input:** Repository path, max commit count, display mode (flat/tree)  
**Output:** `committed-not-pushed-report.json` (JSON) + terminal display in flat or tree format  
**Purpose:** Finds commits that exist locally but haven't been pushed to remote repositories. Supports both flat text and tree visualization like interactive-dirty-files-tui.

### dirty-files
**Input:** Repository path, max file count  
**Output:** `dirty-files-report.json` (JSON) - Detailed list of dirty files by repository  
**Purpose:** Analyzes dirty repositories and lists specific modified/untracked files

## Terminal UI Components

### file-tree
**Input:** `dirty-files-report.json`  
**Output:** Interactive terminal display of file tree  
**Purpose:** Shows dirty files organized in a tree structure with repository headers

### git-tui
**Input:** Git status data from child modules  
**Output:** Terminal UI displaying git status information  
**Purpose:** Terminal interface for viewing git repository status

### dirty-files-tui
**Input:** `dirty-files-report.json`  
**Output:** Terminal UI displaying dirty files analysis  
**Purpose:** Terminal interface for viewing dirty files report

### interactive-dirty-files-tui
**Input:** `dirty-files-report.json`  
**Output:** Interactive terminal UI with tree/flat view toggle  
**Purpose:** Advanced TUI for dirty files with view switching (spacebar) and refresh (R/r)

### three-pane-tui
**Input:** Configurable content for three panes  
**Output:** Three-pane terminal layout with file colorization  
**Purpose:** Demonstrates three-pane TUI layout with cyberpunk color scheme

### hello-tui
**Input:** None (demo component)  
**Output:** Terminal UI displaying "Hello World"  
**Purpose:** Simple TUI demonstration with centered text and timed display

## Utility Components

### git-status
**Input:** Repository path, check interval  
**Output:** `.report` file with git status information  
**Purpose:** Monitors basic git repository status and changes

### terminal
**Input:** None  
**Output:** Current terminal window dimensions  
**Purpose:** Prints terminal size information (rows x columns)

### hello
**Input:** None (demo component)  
**Output:** "Hello World" message to stdout  
**Purpose:** Simple demonstration of the infinite index pattern

### test
**Input:** None (test component)  
**Output:** Test output to stdout  
**Purpose:** Basic test component for development

## Infrastructure

### json-utils
**Input:** Various (JSON files, key paths)  
**Output:** JSON parsing/manipulation utilities  
**Purpose:** Provides JSON parsing, value extraction, and manipulation tools for all components

### main
**Input:** `index.json` configuration  
**Output:** Orchestrates child components based on configuration  
**Purpose:** Root orchestrator that executes components in dependency order

## Data Flow

```
git-submodules → git-submodules.report
     ↓
dirty-files → dirty-files-report.json
     ↓
file-tree/interactive-dirty-files-tui → Terminal Display
```

## Configuration

Each component has an `index.json` file defining:
- **metadata:** Component name, description, schema version
- **paths:** Input/output file paths and directories
- **children:** Child components to execute
- **execution:** Mode (sequential/parallel), error handling
- **config:** Component-specific settings

## Building Components

All components use the same compilation pattern:
```bash
cd component-directory
gcc -o component component.c ../json-utils/json-utils.o -lm
```

## Component Status

- ✅ **Working:** git-submodules, dirty-files, file-tree, committed-not-pushed, json-utils
- 🚧 **In Development:** three-pane-tui, interactive-dirty-files-tui
- 🧪 **Demo/Prototype:** hello, hello-tui, terminal, test, git-status, git-tui
- 📦 **Infrastructure:** main, json-utils

## Usage

Run individual components:
```bash
./git-submodules/git-submodules
./dirty-files/dirty-files
./file-tree/file-tree
```

Run the full orchestrator:
```bash
./main
```

Control committed-not-pushed display mode:
```bash
./main --committed-not-pushed-tree    # Tree visualization
./main --committed-not-pushed-flat    # Traditional flat text
```
