# repoWatch Feature List - Zig Rewrite

**Category**: `development-tools`
**Tags**: `git`, `tui`, `file-watching`, `terminal-ui`, `repository-monitoring`, `zig-rewrite`

## Summary

Complete feature specification for rewriting repoWatch in Zig. This document serves as the foundation for the architecture.md and implementation plan. All features are extracted from the working Python implementation.

## Context

repoWatch is a terminal user interface (TUI) application that provides real-time monitoring of git repository changes. The application displays file status information in a three-pane layout and uses file system watching to detect changes instantly.

The current Python implementation uses Textual for the TUI, watchdog for file monitoring, and GitPython for git operations. The Zig rewrite will need to implement all core functionality natively.

---

## CORE FEATURES

### 1. Git Repository Integration

**1.1 Repository Detection & Validation**
- Detect if directory is a valid git repository (.git folder exists)
- Validate repository accessibility and permissions
- Handle repository path resolution (relative/absolute paths)

**1.2 Git Status Operations**
- Get list of uncommitted files (staged, unstaged, untracked)
- Get recent commits since session start (configurable time window)
- Extract files changed in commits (from commit.stats.files)
- Handle git operation errors gracefully (network issues, corrupted repos)

**1.3 Commit Monitoring**
- Track commits made during current session
- Filter commits by time (since session start or configurable duration)
- Extract file paths from commit statistics
- Handle commit iteration and pagination (max_count parameter)

### 2. File System Watching

**2.1 Inotify-Based Monitoring**
- Watch entire repository directory recursively
- Detect file modifications (on_modified)
- Detect file creation (on_created)
- Detect file deletion (on_deleted)
- Detect file moves (on_moved)

**2.2 File Filtering**
- Exclude directories from events
- Skip git-related files and directories (.git/*)
- Filter temporary files (common patterns: .tmp, .swp, .swo, ~, .bak)
- Convert absolute paths to repository-relative paths

**2.3 Event Processing**
- Queue file change events for UI updates
- Handle high-frequency file changes efficiently
- Maintain timestamp tracking for each file change
- Support concurrent file modifications

### 3. Terminal User Interface (TUI)

**3.1 Three-Pane Layout**
- **Left Pane**: Uncommitted Changes (staged, unstaged, untracked files)
- **Middle Pane**: Committed Files (recent commits since session start)
- **Right Pane**: Active Changes (currently being modified files with animations)

**3.2 UI Components**
- Header with application title and repository name
- Footer with keybind reference
- Status bar with session statistics (duration, file counts)
- Scrollable file lists in each pane
- Real-time updates without full screen refresh

**3.3 Visual Design**
- Bordered panes with consistent spacing
- Color-coded file indicators (📄 for uncommitted, ✅ for committed, ⠋ for active)
- Centered pane titles with bold formatting
- Responsive layout (panes resize with terminal width)

### 4. Animation System

**4.1 Active Change Indicators**
- Spinning animation frames: ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
- Animation lasts exactly 1 second per file change
- 10 FPS animation loop (100ms intervals)
- Automatic cleanup of expired animations

**4.2 Animation State Management**
- Track active files with timestamps
- Remove expired animations automatically
- Update display when files expire
- Handle concurrent animations for multiple files

### 5. Session Management

**5.1 Session Tracking**
- Record session start time
- Track session duration in minutes
- Maintain session statistics (file counts, duration)

**5.2 Status Updates**
- Real-time status bar updates
- Periodic UI refresh cycles
- Handle UI updates from background threads
- Maintain consistent display state

### 6. Configuration & Environment

**6.1 Environment Variables**
- `REPO_WATCH_REPO_PATH`: Repository path (default: current directory)
- Customizable through command line arguments
- Path expansion and validation

**6.2 Runtime Configuration**
- Configurable commit history depth (max_count)
- Adjustable animation duration
- Customizable file filtering patterns
- Theme/color scheme options

---

## SECONDARY FEATURES

### 7. Error Handling & Resilience

**7.1 Git Operation Failures**
- Handle corrupted repositories
- Network issues during git operations
- Permission denied on repository access
- Graceful degradation (show error messages instead of crashing)

**7.2 File System Issues**
- Handle permission denied on file access
- Recover from file system monitoring failures
- Handle rapid file creation/deletion
- Continue operation despite individual file errors

**7.3 TUI Resilience**
- Handle terminal resize events
- Recover from display corruption
- Maintain state during UI updates
- Graceful shutdown on errors

### 8. Performance Optimizations

**8.1 Efficient Updates**
- Debounced UI updates to prevent flicker
- Batched file system event processing
- Lazy loading of large file lists
- Memory-efficient file tracking

**8.2 Resource Management**
- Proper cleanup of file watchers on exit
- Memory cleanup for expired animations
- Efficient git operation caching
- Low CPU usage during idle periods

### 9. User Experience Enhancements

**9.1 Keyboard Navigation**
- Tab/Shift+Tab for pane navigation
- Q/Escape/Ctrl+C for application exit
- Intuitive keybind display in footer

**9.2 Visual Feedback**
- Loading states during initialization
- Empty state messages ("No uncommitted changes", etc.)
- Real-time visual indicators for all operations
- Consistent iconography and formatting

**9.3 Accessibility**
- High contrast color schemes
- Keyboard-only operation
- Screen reader friendly output
- Configurable text sizes

---

## IMPLEMENTATION-SPECIFIC FEATURES

### 10. Zig-Specific Requirements

**10.1 Native File System Watching**
- Implement inotify equivalent in Zig
- Cross-platform file watching (Linux/macOS/Windows support)
- Low-level system call integration
- Efficient event handling without external dependencies

**10.2 Git Operations in Zig**
- Native git repository parsing
- Implement git status commands
- Parse git log output
- Handle git object database access

**10.3 Terminal UI Framework**
- Implement TUI widgets in Zig
- Handle terminal control sequences
- Real-time display updates
- Cross-terminal compatibility

**10.4 Async Runtime**
- Implement async event loops in Zig
- Handle concurrent operations
- Timer-based animations
- Non-blocking I/O operations

### 11. Build & Deployment

**11.1 Build System**
- Zig build configuration
- Dependency management
- Cross-compilation support
- Static binary generation

**11.2 Packaging**
- Single binary distribution
- No runtime dependencies
- Auto-installation of dependencies
- Update mechanism

### 12. Testing Infrastructure

**12.1 Unit Tests**
- Git operation testing
- File watching simulation
- UI component testing
- Animation system verification

**12.2 Integration Tests**
- End-to-end repository monitoring
- Terminal UI interaction testing
- Performance benchmarking
- Cross-platform compatibility

---

## NON-FEATURES (EXPLICITLY EXCLUDED)

### What NOT to Implement

**External Dependencies**
- No Python dependencies (textual, watchdog, gitpython)
- No external TUI libraries
- No external git bindings
- Pure Zig implementation

**Complex Features**
- No networking/remote repository support
- No git push/pull operations
- No advanced git features (branching, merging)
- No plugin system

**Platform-Specific Features**
- No Windows-specific optimizations initially
- No macOS-specific features initially
- Linux-first implementation

**Advanced UI Features**
- No mouse support
- No advanced theming system
- No customizable layouts
- Fixed three-pane design

---

## SUCCESS CRITERIA

### Functional Requirements
- [ ] Monitor git repository file changes in real-time
- [ ] Display uncommitted, committed, and active file changes
- [ ] Provide three-pane terminal UI with animations
- [ ] Handle all git operations natively in Zig
- [ ] Support file system watching without external libraries

### Performance Requirements
- [ ] Sub-second response to file changes
- [ ] Low CPU usage during monitoring
- [ ] Memory efficient for large repositories
- [ ] Smooth 10 FPS animations

### Quality Requirements
- [ ] No crashes on common error conditions
- [ ] Graceful handling of corrupted repositories
- [ ] Clean shutdown and resource cleanup
- [ ] Cross-platform compatibility (Linux/macOS/Windows)

### User Experience Requirements
- [ ] Intuitive three-pane layout
- [ ] Clear visual feedback for all operations
- [ ] Responsive keyboard navigation
- [ ] Professional terminal appearance

---

## MIGRATION NOTES

### From Python Implementation
- **Textual → Native Zig TUI**: Implement terminal widgets from scratch
- **watchdog → Zig inotify**: Direct system call integration
- **GitPython → Native Zig**: Parse git commands and object files directly
- **asyncio → Zig async**: Use Zig's async runtime features

### Architecture Changes
- **Monolithic → Modular**: Break into separate components (git, fs, tui)
- **Python OOP → Zig structs**: Convert classes to Zig data structures
- **Dynamic → Static**: Compile-time optimizations where possible
- **Garbage collected → Manual memory**: Explicit memory management

### Performance Expectations
- **Startup time**: < 100ms (vs Python's ~500ms)
- **Memory usage**: < 10MB (vs Python's ~50MB+)
- **CPU usage**: < 1% during monitoring (vs Python's ~5%)
- **Binary size**: < 5MB (vs Python's dependency requirements)

---

## DEVELOPMENT ROADMAP

### Phase 1: Core Infrastructure
1. Git repository parsing and status operations
2. File system watching implementation
3. Basic terminal output (no TUI yet)

### Phase 2: Terminal UI
1. Basic three-pane layout
2. Static file display (no real-time updates)
3. Keyboard navigation framework

### Phase 3: Real-Time Features
1. Live file watching integration
2. Animation system implementation
3. Real-time UI updates

### Phase 4: Polish & Optimization
1. Error handling and edge cases
2. Performance optimizations
3. Cross-platform compatibility
4. Testing and documentation
