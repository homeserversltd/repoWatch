# Makefile for three-pane-tui modular system

CC = clang
CFLAGS = -I.. -I../json-utils -Wall -Wextra -g
LDFLAGS = -lm

# Object files for each module
CORE_OBJS = core/core.o
STYLES_OBJS = styles/styles.o
DATA_OBJS = data/data.o
UI_OBJS = ui/ui.o
MAIN_OBJS = main/main.o

# All object files
ALL_OBJS = $(CORE_OBJS) $(STYLES_OBJS) $(DATA_OBJS) $(UI_OBJS) $(MAIN_OBJS)

# JSON utils dependencies (compiled as library only, no main)
JSON_UTILS = ../json-utils/json-utils.o ../json-utils/get-value.o

# Compile flags for JSON utils (library only, no main function)
JSON_CFLAGS = $(CFLAGS) -DGET_VALUE_LIBRARY_ONLY

# JSON utils object files (compiled as library)
../json-utils/json-utils.o: ../json-utils/json-utils.c
	$(CC) $(JSON_CFLAGS) -c -o $@ $<

../json-utils/get-value.o: ../json-utils/get-value.c
	$(CC) $(JSON_CFLAGS) -c -o $@ $<

# Main target
three-pane-tui: $(ALL_OBJS) $(JSON_UTILS) three-pane-tui.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Individual module compilations
core/core.o: core/core.c three-pane-tui.h
	$(CC) $(CFLAGS) -c -o $@ $<

styles/styles.o: styles/styles.c three-pane-tui.h
	$(CC) $(CFLAGS) -c -o $@ $<

data/data.o: data/data.c three-pane-tui.h
	$(CC) $(CFLAGS) -c -o $@ $<

ui/ui.o: ui/ui.c three-pane-tui.h
	$(CC) $(CFLAGS) -c -o $@ $<

main/main.o: main/main.c three-pane-tui.h
	$(CC) $(CFLAGS) -c -o $@ $<

three-pane-tui.o: three-pane-tui.c three-pane-tui.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean target
clean:
	rm -f three-pane-tui *.o */*.o

# Phony targets
.PHONY: clean
