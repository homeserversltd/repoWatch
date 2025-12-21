# repoWatch Architecture - Zig Rewrite

**Category**: `architecture`
**Tags**: `zig`, `tui`, `git`, `file-watching`, `system-architecture`, `rewrite`

## Summary

Complete architectural specification for rewriting repoWatch in Zig using the infinite index system. This document defines the modular architecture, component relationships, and implementation strategy for a native Zig implementation that eliminates all Python dependencies.

## Context

repoWatch is being rewritten from Python to Zig to achieve:
- **Zero runtime dependencies** - Single static binary
- **Native performance** - Sub-millisecond response times
- **Cross-platform compatibility** - Linux, macOS, Windows support
- **Memory safety** - Zig's compile-time guarantees
- **Maintainability** - Clean modular architecture using infinite index pattern

The rewrite eliminates:
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

### 3. Native Zig Implementation
- **No external dependencies**: Pure Zig standard library usage
- **Cross-platform**: Unified API across operating systems
- **Performance-first**: Zero-cost abstractions and optimizations
- **Memory safety**: Compile-time guarantees and manual memory management

---

## CORE ARCHITECTURE COMPONENTS

### Root Orchestrator (`index.py`)
Entry point that coordinates all subsystems:
- **Configuration loading** from index.json
- **Environment setup** and path resolution
- **Child module execution** in dependency order
- **Error handling** and graceful shutdown

### Git Subsystem (`git/index.py`)
Native git repository operations:
- **Repository validation** and initialization
- **Status operations** (staged, unstaged, untracked files)
- **Commit enumeration** with file extraction
- **Error handling** for corrupted repositories

### File System Subsystem (`fs/index.py`)
Cross-platform file watching:
- **Inotify integration** (Linux) / FSEvents (macOS) / ReadDirectoryChangesW (Windows)
- **Event filtering** (exclude .git, temp files, directories)
- **Path normalization** (absolute → relative conversion)
- **Event queuing** for UI updates

### TUI Subsystem (`tui/index.py`)
Terminal user interface:
- **Three-pane layout** (uncommitted, committed, active changes)
- **Real-time updates** without screen flicker
- **Animation system** (1-second spinning indicators)
- **Keyboard handling** (navigation, quit commands)

### Animation Subsystem (`animation/index.py`)
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
├── index.py           # Main orchestrator
├── git/
│   ├── index.json     # Git subsystem config
│   ├── index.py       # Git operations
│   ├── status/
│   │   ├── index.json # Status operations config
│   │   └── index.py   # Status implementation
│   └── commits/
│       ├── index.json # Commit operations config
│       └── index.py   # Commit implementation
├── fs/
│   ├── index.json     # File system config
│   ├── index.py       # File watching orchestrator
│   ├── linux/
│   │   ├── index.json # Linux-specific config
│   │   └── index.py   # inotify implementation
│   ├── macos/
│   │   ├── index.json # macOS-specific config
│   │   └── index.py   # FSEvents implementation
│   └── windows/
│       ├── index.json # Windows-specific config
│       └── index.py   # ReadDirectoryChangesW implementation
├── tui/
│   ├── index.json     # TUI config
│   ├── index.py       # TUI orchestrator
│   ├── layout/
│   │   ├── index.json # Layout config
│   │   └── index.py   # Three-pane layout
│   ├── widgets/
│   │   ├── index.json # Widget config
│   │   └── index.py   # UI components
│   └── input/
│       ├── index.json # Input config
│       └── index.py   # Keyboard handling
└── animation/
    ├── index.json     # Animation config
    ├── index.py       # Animation system
    ├── frames/
    │   ├── index.json # Frame data config
    │   └── index.py   # Spinner frames
    └── timing/
        ├── index.json # Timing config
        └── index.py   # Duration management
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
  "children": ["${platform}"],
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
```zig
pub const GitSubsystem = struct {
    repo_path: []const u8,
    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator, config: Config) !GitSubsystem
    pub fn deinit(self: *GitSubsystem) void
    pub fn validateRepo(self: GitSubsystem) !bool
    pub fn getUncommittedFiles(self: GitSubsystem, allocator: std.mem.Allocator) ![][]const u8
    pub fn getRecentCommits(self: GitSubsystem, since: i64, max_count: usize, allocator: std.mem.Allocator) ![]CommitInfo
};

pub const CommitInfo = struct {
    hash: []const u8,
    message: []const u8,
    files: [][]const u8,
    timestamp: i64,
};
```

### File System Interface
```zig
pub const FsSubsystem = struct {
    watch_path: []const u8,
    allocator: std.mem.Allocator,
    callback: *const fn (event_type: EventType, file_path: []const u8) void,

    pub fn init(allocator: std.mem.Allocator, config: Config, callback: anytype) !FsSubsystem
    pub fn deinit(self: *FsSubsystem) void
    pub fn startWatching(self: *FsSubsystem) !void
    pub fn stopWatching(self: *FsSubsystem) void
};

pub const EventType = enum {
    modified,
    created,
    deleted,
    moved,
};
```

### TUI Interface
```zig
pub const TuiSubsystem = struct {
    allocator: std.mem.Allocator,
    terminal: Terminal,

    pub fn init(allocator: std.mem.Allocator, config: Config) !TuiSubsystem
    pub fn deinit(self: *TuiSubsystem) void
    pub fn render(self: *TuiSubsystem) !void
    pub fn updateUncommittedFiles(self: *TuiSubsystem, files: [][]const u8) !void
    pub fn updateCommittedFiles(self: *TuiSubsystem, files: [][]const u8) !void
    pub fn updateActiveChanges(self: *TuiSubsystem, changes: []ActiveChange) !void
    pub fn handleInput(self: *TuiSubsystem) !InputEvent
};

pub const ActiveChange = struct {
    file_path: []const u8,
    spinner_frame: u8,
    timestamp: i64,
};
```

### Animation Interface
```zig
pub const AnimationSubsystem = struct {
    allocator: std.mem.Allocator,
    active_changes: std.StringHashMap(i64),
    frame_index: u8,
    frames: []const u8,

    pub fn init(allocator: std.mem.Allocator, config: Config) !AnimationSubsystem
    pub fn deinit(self: *AnimationSubsystem) void
    pub fn addActiveFile(self: *AnimationSubsystem, file_path: []const u8) !void
    pub fn update(self: *AnimationSubsystem, current_time: i64) ![]ActiveChange
    pub fn getFrame(self: AnimationSubsystem) u8
};
```

---

## ERROR HANDLING STRATEGY

### Error Types
```zig
pub const RepoWatchError = error{
    InvalidRepository,
    GitOperationFailed,
    FileSystemError,
    TerminalNotSupported,
    OutOfMemory,
    PermissionDenied,
    InvalidConfiguration,
};
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

## CROSS-PLATFORM ARCHITECTURE

### Platform Detection
```zig
const builtin = @import("builtin");
const target = builtin.target;

pub const Platform = enum {
    linux,
    macos,
    windows,
    unknown,

    pub fn detect() Platform {
        return switch (target.os.tag) {
            .linux => .linux,
            .macos => .macos,
            .windows => .windows,
            else => .unknown,
        };
    }
};
```

### Platform-Specific Implementations
- **Linux**: inotify for efficient file watching
- **macOS**: FSEvents for native file monitoring
- **Windows**: ReadDirectoryChangesW for change notifications
- **Fallback**: Polling-based implementation for unsupported platforms

### Unified API
All platform implementations conform to the same interface, allowing the rest of the system to remain platform-agnostic.

---

## BUILD SYSTEM ARCHITECTURE

### Zig Build Configuration
```zig
// build.zig
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "repowatch",
        .root_source_file = .{ .path = "src/main.zig" },
        .target = target,
        .optimize = optimize,
    });

    // Add dependencies
    exe.addIncludePath("src");

    // Install
    b.installArtifact(exe);
}
```

### Build Targets
- **Debug**: Full debugging, safety checks enabled
- **ReleaseSafe**: Optimizations enabled, safety checks enabled
- **ReleaseFast**: Maximum optimizations, minimal safety checks
- **ReleaseSmall**: Size optimizations, stripped binary

### Cross-Compilation
- **Native builds** for development platform
- **Cross-compilation** for distribution targets
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
- **Platform compatibility**: Automated testing on all target platforms
- **Load testing**: High-frequency file operations
- **Longevity testing**: Extended session stability

### Test Organization
```
test/
├── unit/
│   ├── git_test.zig
│   ├── fs_test.zig
│   ├── tui_test.zig
│   └── animation_test.zig
├── integration/
│   ├── workflow_test.zig
│   └── platform_test.zig
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
1. **Git subsystem** implementation
2. **File system watching** (Linux first)
3. **Basic CLI interface** (no TUI)
4. **Unit testing framework**

### Phase 2: Terminal UI (Month 2)
1. **TUI framework** development
2. **Three-pane layout** implementation
3. **Real-time updates** integration
4. **Keyboard navigation**

### Phase 3: Polish & Optimization (Month 3)
1. **Animation system** completion
2. **Cross-platform support** (macOS, Windows)
3. **Performance optimization**
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
- [ ] Cross-platform compatibility (Linux/macOS/Windows)
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
- **Cross-platform file watching**: Start with Linux, expand to other platforms
- **Git parsing complexity**: Use proven parsing libraries, validate thoroughly

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

This architecture provides a solid foundation for rewriting repoWatch in Zig. The infinite index pattern ensures maintainable modularity, while Zig's native capabilities eliminate external dependencies and provide superior performance. The phased approach allows for incremental development and early validation of core functionality.

The resulting system will be a fast, reliable, and maintainable git repository monitor that demonstrates the power of native development with modern systems programming languages.
