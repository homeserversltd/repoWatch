# repoWatch Architecture - C Implementation

**Category**: `architecture`
**Tags**: `c`, `tui`, `git`, `file-watching`, `system-architecture`, `implementation`

## Summary

Complete architectural specification for implementing repoWatch in C using the infinite index system. This document defines the modular architecture, component relationships, and implementation strategy for a Linux-native C implementation that eliminates all Python dependencies.

## Context

repoWatch is being implemented in C to achieve:
- **Zero runtime dependencies** - Single static binary
- **Native performance** - Sub-millisecond response times
- **Linux-native** - Optimized for Linux systems with inotify
- **Memory efficiency** - Manual memory management and optimization
- **Maintainability** - Clean modular architecture using infinite index pattern

The implementation eliminates:
- Textual (Python TUI framework)
- watchdog (Python file watching)
- GitPython (Python git bindings)
- asyncio (Python async runtime)

---

## ARCHITECTURAL PRINCIPLES

### 1. Infinite Index Pattern
- **Configuration-driven**: All modules use index.json for configuration
- **Hierarchical orchestration**: Parent modules execute child modules recursively
- **Environment expansion**: Paths support `${VAR:-default}` syntax
- **Modular composition**: Components are independently testable and replaceable

### 2. Component Isolation
- **Single responsibility**: Each module handles one concern
- **Clean interfaces**: Well-defined APIs between components
- **Dependency injection**: Configuration passed from parent to child
- **Error boundaries**: Failures contained within module scope

### 3. Native C Implementation
- **No external dependencies**: Pure C standard library and system calls
- **Linux-optimized**: Direct system call integration for Linux systems
- **Performance-first**: Low-level optimizations and efficient memory usage
- **Memory efficiency**: Manual memory management and resource control

---

## CORE ARCHITECTURE COMPONENTS

### Root Orchestrator (`index.c`)
Entry point that coordinates all subsystems:
- **Configuration loading** from index.json
- **Environment setup** and path resolution
- **Child module execution** in dependency order
- **Error handling** and graceful shutdown

### Git Subsystem (`git/index.c`, `git/git.h`)
Native git repository operations:
- **Repository validation** and initialization
- **Status operations** (staged, unstaged, untracked files)
- **Commit enumeration** with file extraction
- **Error handling** for corrupted repositories

### File System Subsystem (`fs/index.c`, `fs/fs.h`)
Linux-native file watching:
- **Inotify integration** for Linux systems
- **Event filtering** (exclude .git, temp files, directories)
- **Path normalization** (absolute → relative conversion)
- **Event queuing** for UI updates

### TUI Subsystem (`tui/index.c`, `tui/tui.h`)
Terminal user interface:
- **Three-pane layout** (uncommitted, committed, active changes)
- **Real-time updates** without screen flicker
- **Animation system** (1-second spinning indicators)
- **Keyboard handling** (navigation, quit commands)

### Animation Subsystem (`animation/index.c`, `animation/animation.h`)
Visual feedback system:
- **Frame management** (10 FPS spinner animations)
- **Timing coordination** (1-second active file duration)
- **State tracking** (file → timestamp mapping)
- **Cleanup automation** (expired animation removal)

---

## MODULE HIERARCHY

```
repoWatch/
├── index.json          # Root configuration
├── index.c            # Main orchestrator
├── index.h            # Main header
├── git/
│   ├── index.json     # Git subsystem config
│   ├── index.c        # Git operations
│   ├── git.h          # Git subsystem header
│   ├── status/
│   │   ├── index.json # Status operations config
│   │   ├── index.c    # Status implementation
│   │   └── status.h   # Status header
│   └── commits/
│       ├── index.json # Commit operations config
│       ├── index.c    # Commit implementation
│       └── commits.h  # Commits header
├── fs/
│   ├── index.json     # File system config
│   ├── index.c        # File watching orchestrator
│   ├── fs.h           # File system header
│   └── linux/
│       ├── index.json # Linux-specific config
│       ├── index.c    # inotify implementation
│       └── linux.h    # Linux-specific header
├── tui/
│   ├── index.json     # TUI config
│   ├── index.c        # TUI orchestrator
│   ├── tui.h          # TUI header
│   ├── layout/
│   │   ├── index.json # Layout config
│   │   ├── index.c    # Three-pane layout
│   │   └── layout.h   # Layout header
│   ├── widgets/
│   │   ├── index.json # Widget config
│   │   ├── index.c    # UI components
│   │   └── widgets.h  # Widgets header
│   └── input/
│       ├── index.json # Input config
│       ├── index.c    # Keyboard handling
│       └── input.h    # Input header
└── animation/
    ├── index.json     # Animation config
    ├── index.c        # Animation system
    ├── animation.h    # Animation header
    ├── frames/
    │   ├── index.json # Frame data config
    │   ├── index.c    # Spinner frames
    │   └── frames.h   # Frames header
    └── timing/
        ├── index.json # Timing config
        ├── index.c    # Duration management
        └── timing.h   # Timing header
```

---

## DATA FLOW ARCHITECTURE

### Event Flow
```
File Change Event
    ↓
FS Watcher (inotify/FSEvents/ReadDirectoryChangesW)
    ↓
Event Filtering (exclude .git, temp files)
    ↓
Path Normalization (absolute → relative)
    ↓
Animation System (add to active_changes map)
    ↓
TUI Update (refresh active changes pane)
```

### Status Update Flow
```
Timer Event (2-second intervals)
    ↓
Git Status Query (uncommitted files)
    ↓
Git Commit Query (recent commits)
    ↓
File Extraction (from commit stats)
    ↓
TUI Update (refresh all panes + status bar)
```

### Animation Flow
```
Animation Timer (100ms intervals)
    ↓
Active Changes Check (current timestamp vs file timestamps)
    ↓
Expired File Removal (files > 1 second old)
    ↓
Frame Index Update (cycle through spinner frames)
    ↓
TUI Update (refresh active changes pane)
```

---

## CONFIGURATION HIERARCHY

### Root Configuration (index.json)
```json
{
  "metadata": {
    "schema_version": "1.0.0",
    "name": "repoWatch",
    "description": "Native Zig git repository monitor"
  },
  "paths": {
    "repo_path": "${REPO_WATCH_REPO_PATH:-.}",
    "config_dir": "${XDG_CONFIG_HOME:-~/.config}/repowatch",
    "cache_dir": "${XDG_CACHE_HOME:-~/.cache}/repowatch"
  },
  "children": ["git", "fs", "tui", "animation"],
  "execution": {
    "mode": "parallel",
    "continue_on_error": false,
    "parallel": true
  },
  "config": {
    "session_timeout": 3600,
    "max_commits": 20,
    "animation_fps": 10,
    "ui_refresh_rate": 2
  }
}
```

### Git Subsystem Configuration
```json
{
  "metadata": {
    "schema_version": "1.0.0",
    "name": "Git Subsystem",
    "description": "Native git repository operations"
  },
  "paths": {
    "git_dir": "${repo_path}/.git",
    "index_file": "${repo_path}/.git/index"
  },
  "children": ["status", "commits"],
  "execution": {
    "mode": "sequential",
    "continue_on_error": true,
    "parallel": false
  },
  "config": {
    "max_commit_history": 50,
    "follow_renames": true,
    "ignore_whitespace": false
  }
}
```

### File System Subsystem Configuration
```json
{
  "metadata": {
    "schema_version": "1.0.0",
    "name": "File System Subsystem",
    "description": "Cross-platform file watching"
  },
  "paths": {
    "watch_path": "${repo_path}",
    "exclude_patterns": [".git", "*.tmp", "*.swp"]
  },
  "children": ["linux"],
  "execution": {
    "mode": "sequential",
    "continue_on_error": false,
    "parallel": false
  },
  "config": {
    "recursive": true,
    "follow_symlinks": false,
    "debounce_ms": 100
  }
}
```

---

## COMPONENT INTERFACES

### Git Interface
```c
typedef struct {
    char* repo_path;
    // Internal state
} git_subsystem_t;

typedef struct {
    char* hash;
    char* message;
    char** files;
    size_t file_count;
    time_t timestamp;
} commit_info_t;

// Function declarations
int git_init(git_subsystem_t* git, const char* repo_path);
void git_cleanup(git_subsystem_t* git);
int git_validate_repo(git_subsystem_t* git);
char** git_get_uncommitted_files(git_subsystem_t* git, size_t* count);
commit_info_t* git_get_recent_commits(git_subsystem_t* git, time_t since, size_t max_count, size_t* result_count);
```

### File System Interface
```c
typedef enum {
    EVENT_MODIFIED,
    EVENT_CREATED,
    EVENT_DELETED,
    EVENT_MOVED,
} event_type_t;

typedef struct {
    char* watch_path;
    int inotify_fd;
    void (*callback)(event_type_t event_type, const char* file_path);
    // Internal state
} fs_subsystem_t;

// Function declarations
int fs_init(fs_subsystem_t* fs, const char* watch_path, void (*callback)(event_type_t, const char*));
void fs_cleanup(fs_subsystem_t* fs);
int fs_start_watching(fs_subsystem_t* fs);
void fs_stop_watching(fs_subsystem_t* fs);
```

### TUI Interface
```c
typedef struct {
    char* file_path;
    char spinner_frame;
    time_t timestamp;
} active_change_t;

typedef struct {
    // Terminal state
    char** uncommitted_files;
    size_t uncommitted_count;
    char** committed_files;
    size_t committed_count;
    active_change_t* active_changes;
    size_t active_count;
} tui_subsystem_t;

// Function declarations
int tui_init(tui_subsystem_t* tui);
void tui_cleanup(tui_subsystem_t* tui);
int tui_render(tui_subsystem_t* tui);
int tui_update_uncommitted_files(tui_subsystem_t* tui, char** files, size_t count);
int tui_update_committed_files(tui_subsystem_t* tui, char** files, size_t count);
int tui_update_active_changes(tui_subsystem_t* tui, active_change_t* changes, size_t count);
int tui_handle_input(tui_subsystem_t* tui);
```

### Animation Interface
```c
typedef struct {
    // Hash map equivalent for active changes
    char** active_files;
    time_t* timestamps;
    size_t active_count;
    size_t frame_index;
    const char* frames[10];
} animation_subsystem_t;

// Function declarations
int animation_init(animation_subsystem_t* anim);
void animation_cleanup(animation_subsystem_t* anim);
int animation_add_active_file(animation_subsystem_t* anim, const char* file_path);
active_change_t* animation_update(animation_subsystem_t* anim, time_t current_time, size_t* result_count);
char animation_get_frame(animation_subsystem_t* anim);
```

---

## ERROR HANDLING STRATEGY

### Error Types
```c
#define REPOWATCH_SUCCESS 0
#define REPOWATCH_INVALID_REPOSITORY -1
#define REPOWATCH_GIT_OPERATION_FAILED -2
#define REPOWATCH_FILESYSTEM_ERROR -3
#define REPOWATCH_TERMINAL_NOT_SUPPORTED -4
#define REPOWATCH_OUT_OF_MEMORY -5
#define REPOWATCH_PERMISSION_DENIED -6
#define REPOWATCH_INVALID_CONFIGURATION -7
```

### Error Recovery
- **Git failures**: Continue with empty file lists, show error in status bar
- **File system failures**: Fall back to polling mode, reduced functionality
- **TUI failures**: Attempt graceful degradation to basic text output
- **Memory failures**: Immediate shutdown with cleanup

### Logging Strategy
- **Structured logging** with severity levels
- **Error context** preservation for debugging
- **User-friendly messages** in TUI status bar
- **Development logs** for troubleshooting

---

## PERFORMANCE TARGETS

### Latency Requirements
- **File change detection**: < 10ms from filesystem event to UI update
- **Git operations**: < 100ms for status queries
- **UI rendering**: < 16ms (60 FPS) for smooth animations
- **Startup time**: < 50ms to first display

### Resource Usage
- **Memory**: < 8MB resident set size
- **CPU**: < 1% average usage during monitoring
- **Disk I/O**: Minimal, cached git operations
- **Binary size**: < 2MB stripped release binary

### Scalability
- **Large repositories**: Handle 100K+ files efficiently
- **High-frequency changes**: Process 100+ file events per second
- **Long sessions**: Stable memory usage over 24+ hours
- **Terminal resizing**: Instant adaptation to new dimensions

---

## LINUX-NATIVE ARCHITECTURE

### Linux-Specific Implementation
- **inotify**: Direct C integration with Linux kernel file watching
- **System calls**: Raw POSIX system calls for maximum performance
- **Kernel-level events**: Immediate notification of file system changes

### Linux-Optimized Design
The C implementation is specifically designed for Linux systems, leveraging:
- Direct inotify system call integration
- Linux-specific file system behaviors
- Native terminal control sequences
- Systemd integration potential

---

## BUILD SYSTEM ARCHITECTURE

### C Build Configuration
```makefile
# Makefile
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -I.
LDFLAGS =

SRC = $(wildcard */*.c) $(wildcard */*/*.c) main.c
OBJ = $(SRC:.c=.o)
TARGET = repowatch

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

install: $(TARGET)
	install $(TARGET) /usr/local/bin/

.PHONY: clean install
```

### Build Targets
- **Debug**: Full debugging with -g, no optimizations
- **Release**: Optimized build with -O2, stripped binary
- **Static**: Statically linked binary for maximum portability

### Cross-Compilation
- **Native builds** for development platform
- **Cross-compilation** for different Linux architectures
- **Static linking** for dependency-free binaries

---

## TESTING STRATEGY

### Unit Testing
- **Component isolation**: Each module tested independently
- **Mock interfaces**: Simulated filesystem and git operations
- **Edge cases**: Error conditions, boundary values
- **Performance tests**: Latency and resource usage verification

### Integration Testing
- **End-to-end workflows**: File change → UI update cycle
- **Linux-specific testing**: inotify behavior validation
- **Load testing**: High-frequency file operations on Linux file systems
- **Longevity testing**: Extended session stability on Linux systems

### Test Organization
```
test/
├── unit/
│   ├── git_test.c
│   ├── fs_test.c
│   ├── tui_test.c
│   └── animation_test.c
├── integration/
│   ├── workflow_test.c
│   └── platform_test.c
└── fixtures/
    ├── test_repo/
    └── mock_data/
```

---

## DEPLOYMENT ARCHITECTURE

### Distribution Strategy
- **Single binary**: No installation required, just execute
- **Zero dependencies**: Works on any compatible system
- **Auto-updates**: Built-in update mechanism (future feature)
- **Configuration**: XDG Base Directory specification compliance

### Installation
```bash
# Download and make executable
curl -L https://github.com/homeserver/repowatch/releases/latest/download/repowatch-linux-x64 -o repowatch
chmod +x repowatch

# Run directly
./repowatch /path/to/repo
```

### Configuration
- **Global config**: `~/.config/repowatch/config.json`
- **Repository config**: `./.repowatch.json` (optional)
- **Environment variables**: Override defaults
- **Command-line flags**: Runtime configuration

---

## MIGRATION PATH

### Phase 1: Core Infrastructure (Month 1)
1. **Git subsystem** C implementation
2. **File system watching** (Linux inotify)
3. **Basic CLI interface** (no TUI)
4. **Unit testing framework**

### Phase 2: Terminal UI (Month 2)
1. **TUI framework** development in C
2. **Three-pane layout** implementation
3. **Real-time updates** integration
4. **Keyboard navigation**

### Phase 3: Polish & Optimization (Month 3)
1. **Animation system** completion
2. **Performance optimization** for Linux
3. **Memory management** refinement
4. **Documentation and packaging**

### Phase 4: Production Release (Month 4)
1. **Comprehensive testing**
2. **User acceptance testing**
3. **Documentation completion**
4. **Release preparation**

---

## SUCCESS METRICS

### Functional Completeness
- [ ] All features from Python implementation reproduced
- [ ] Linux-native C implementation with inotify support
- [ ] Zero runtime dependencies
- [ ] Native performance (sub-millisecond response times)

### Quality Assurance
- [ ] 100% test coverage for critical paths
- [ ] Memory safety verification (valgrind, sanitizers)
- [ ] Performance benchmarks meeting targets
- [ ] User experience parity with Python version

### Operational Readiness
- [ ] Automated build and release pipeline
- [ ] Comprehensive documentation
- [ ] User-friendly installation and setup
- [ ] Support and maintenance procedures

---

## RISK MITIGATION

### Technical Risks
- **Complex TUI implementation**: Prototype early, build incrementally
- **inotify integration**: Ensure proper event handling and resource management
- **Git parsing complexity**: Use proven parsing approaches, validate thoroughly

### Schedule Risks
- **Learning curve**: Allocate time for Zig proficiency building
- **Integration complexity**: Use modular architecture to isolate issues
- **Testing overhead**: Implement automated testing from day one

### Resource Risks
- **Performance requirements**: Profile early, optimize incrementally
- **Memory safety**: Use Zig's safety features and testing
- **Binary size**: Monitor and optimize build outputs

---

## CONCLUSION

This architecture provides a solid foundation for implementing repoWatch in C. The infinite index pattern ensures maintainable modularity, while C's native capabilities and Linux-specific optimizations eliminate external dependencies and provide superior performance. The phased approach allows for incremental development and early validation of core functionality.

The resulting system will be a fast, reliable, and maintainable Linux-native git repository monitor that leverages inotify for real-time file watching and demonstrates the power of native development with systems programming.
