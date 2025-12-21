# Root Makefile for repoWatch - Build all components with one command
#
# Usage:
#   make all           - Build everything (json-utils + main orchestrator + all components + three-pane-tui)
#   make components    - Build just the child components (not main orchestrator)
#   make main          - Build only the main orchestrator
#   make json-utils    - Build JSON utilities library + standalone programs
#   make [component]   - Build specific component (e.g., make git-submodules, make three-pane-tui)
#   make clean         - Clean all build artifacts
#   make clean-[comp]  - Clean specific component (e.g., make clean-git-submodules)
#
# Components available: git-submodules, committed-not-pushed, dirty-files, file-tree,
#                       dirty-files-tui, hello, hello-tui, terminal, git-status, git-tui,
#                       test, interactive-dirty-files-tui, three-pane-tui

CC = clang
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lm

# Define all components and their source files
COMPONENTS = main git-submodules committed-not-pushed dirty-files file-tree dirty-files-tui hello hello-tui terminal git-status git-tui test interactive-dirty-files-tui

# JSON utils library components (core library only, no main functions)
JSON_UTILS_LIB = json-utils/json-utils.o json-utils/get-value.o

# JSON utils utilities (standalone programs with main functions)
JSON_UTILS_PROGS = json-utils/get-children json-utils/read-report json-utils/set-value json-utils/test-parse

# Build all components
all: json-utils main components three-pane-tui

# Build just the components (not main orchestrator)
components: $(COMPONENTS)

# Build JSON utilities (library + standalone programs)
json-utils: $(JSON_UTILS_LIB) $(JSON_UTILS_PROGS)
	@echo "✓ JSON utilities built"

# Build main orchestrator
main: main.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Main orchestrator built"

# Build individual components
git-submodules: git-submodules/git-submodules.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/git-submodules $^ $(LDFLAGS)
	@echo "✓ git-submodules built"

committed-not-pushed: committed-not-pushed/committed-not-pushed.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/committed-not-pushed $^ $(LDFLAGS)
	@echo "✓ committed-not-pushed built"

dirty-files: dirty-files/dirty-files.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/dirty-files $^ $(LDFLAGS)
	@echo "✓ dirty-files built"

file-changes-watcher: file-changes-watcher/file-changes-watcher.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/file-changes-watcher $^ $(LDFLAGS)
	@echo "✓ file-changes-watcher built"

file-tree: file-tree/file-tree.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/file-tree $^ $(LDFLAGS)
	@echo "✓ file-tree built"

dirty-files-tui: dirty-files-tui/dirty-files-tui.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/dirty-files-tui $^ $(LDFLAGS)
	@echo "✓ dirty-files-tui built"

hello: hello/hello.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/hello $^ $(LDFLAGS)
	@echo "✓ hello built"

hello-tui: hello-tui/hello-tui.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/hello-tui $^ $(LDFLAGS)
	@echo "✓ hello-tui built"

terminal: terminal/terminal.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/terminal $^ $(LDFLAGS)
	@echo "✓ terminal built"

git-status: git-status/git-status.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/git-status $^ $(LDFLAGS)
	@echo "✓ git-status built"

git-tui: git-tui/git-tui.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/git-tui $^ $(LDFLAGS)
	@echo "✓ git-tui built"

test: test/test.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/test $^ $(LDFLAGS)
	@echo "✓ test built"

interactive-dirty-files-tui: interactive-dirty-files-tui/interactive-dirty-files-tui.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@/interactive-dirty-files-tui $^ $(LDFLAGS)
	@echo "✓ interactive-dirty-files-tui built"

# Build three-pane-tui using its own Makefile (depends on JSON utils)
three-pane-tui: $(JSON_UTILS_LIB)
	make -C three-pane-tui
	@echo "✓ three-pane-tui built"

# Compilation rules for object files
main.o: main.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

git-submodules/git-submodules.o: git-submodules/git-submodules.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

committed-not-pushed/committed-not-pushed.o: committed-not-pushed/committed-not-pushed.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

dirty-files/dirty-files.o: dirty-files/dirty-files.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

file-tree/file-tree.o: file-tree/file-tree.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

dirty-files-tui/dirty-files-tui.o: dirty-files-tui/dirty-files-tui.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

hello/hello.o: hello/hello.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

hello-tui/hello-tui.o: hello-tui/hello-tui.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

terminal/terminal.o: terminal/terminal.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

git-status/git-status.o: git-status/git-status.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

git-tui/git-tui.o: git-tui/git-tui.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

test/test.o: test/test.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

interactive-dirty-files-tui/interactive-dirty-files-tui.o: interactive-dirty-files-tui/interactive-dirty-files-tui.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

# JSON utils library compilation (no main functions)
json-utils/json-utils.o: json-utils/json-utils.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -DGET_VALUE_LIBRARY_ONLY -c -o $@ $<

json-utils/get-value.o: json-utils/get-value.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -DGET_VALUE_LIBRARY_ONLY -c -o $@ $<

# JSON utils standalone programs (with main functions)
json-utils/get-children: json-utils/get-children.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

json-utils/read-report: json-utils/read-report.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

json-utils/set-value: json-utils/set-value.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

json-utils/test-parse: json-utils/test-parse.o $(JSON_UTILS_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Individual JSON utils object compilation
json-utils/get-children.o: json-utils/get-children.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

json-utils/read-report.o: json-utils/read-report.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

json-utils/set-value.o: json-utils/set-value.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

json-utils/test-parse.o: json-utils/test-parse.c json-utils/json-utils.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean all build artifacts
clean:
	rm -f main *.o */*.o
	rm -f git-submodules/git-submodules committed-not-pushed/committed-not-pushed
	rm -f dirty-files/dirty-files file-tree/file-tree dirty-files-tui/dirty-files-tui
	rm -f hello/hello hello-tui/hello-tui terminal/terminal
	rm -f git-status/git-status git-tui/git-tui test/test
	rm -f interactive-dirty-files-tui/interactive-dirty-files-tui
	$(MAKE) -C three-pane-tui clean
	@echo "✓ All build artifacts cleaned"

# Clean only specific component
clean-%:
	rm -f $*/$* $*/$*.o
	@echo "✓ Cleaned $*"

# Build only specific component
%:
	@if [ -d "$@" ]; then \
		if [ "$@" = "three-pane-tui" ]; then \
			$(MAKE) -C $@; \
			echo "✓ three-pane-tui built"; \
		else \
			$(MAKE) $@/$@; \
		fi \
	else \
		echo "Error: Component '$@' not found"; \
		exit 1; \
	fi

# Phony targets
.PHONY: all components json-utils main clean clean-% three-pane-tui $(COMPONENTS)
