CC = gcc

CFLAGS = -Wall -Wextra -std=c11
GTK_FLAGS = $(shell pkg-config --cflags --libs gtk4)

TARGET = out/shellix
TEST_TARGET = out/shell_test
GUI_TEST_TARGET = out/terminal_test

SOURCES = \
	src/main.c \
	src/window.c \
	src/terminal.c \
	src/shell.c

TEST_SOURCES = \
	src/shell.c \
	tests/shell_test.c

GUI_TEST_SOURCES = \
	src/shell.c \
	src/terminal.c \
	tests/terminal_test.c

all:
	mkdir -p out
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(GTK_FLAGS)

run: all
	./$(TARGET)

# Exercises the execution engine on its own, so no GTK is needed.
test:
	mkdir -p out
	$(CC) $(CFLAGS) -g -Isrc $(TEST_SOURCES) -o $(TEST_TARGET)
	./$(TEST_TARGET)

# Drives the real terminal widget: types commands and checks the transcript.
# Needs a display.
test-gui:
	mkdir -p out
	$(CC) $(CFLAGS) -g -Isrc $(GUI_TEST_SOURCES) -o $(GUI_TEST_TARGET) $(GTK_FLAGS)
	./$(GUI_TEST_TARGET)

check: test test-gui

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(GUI_TEST_TARGET)

.PHONY: all run test test-gui check clean
