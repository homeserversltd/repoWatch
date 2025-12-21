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
ANIM_OBJS = animations/animations.o

# All object files
ALL_OBJS = $(CORE_OBJS) $(STYLES_OBJS) $(DATA_OBJS) $(UI_OBJS) $(MAIN_OBJS) $(ANIM_OBJS)

# JSON utils dependencies (built by root Makefile, assume they exist)
JSON_UTILS = ../json-utils/json-utils.o ../json-utils/get-value.o

# Main target - assumes JSON utils are already built by root Makefile
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

animations/animations.o: animations/animations.c three-pane-tui.h
	$(CC) $(CFLAGS) -c -o $@ $<

three-pane-tui.o: three-pane-tui.c three-pane-tui.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean target
clean:
	rm -f three-pane-tui *.o */*.o

# Phony targets
.PHONY: clean
