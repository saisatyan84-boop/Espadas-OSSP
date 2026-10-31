CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2 -Iinclude -Isrc -D_GNU_SOURCE -MMD -MP
LDFLAGS := -lncursesw -lm

TARGET  := bin/wintop
SRCS    := src/main.c src/host_ipc.c src/linux_sys.c src/ui.c src/cJSON.c
OBJS    := $(SRCS:.c=.o)

-include $(OBJS:.o=.d)
.DEFAULT_GOAL := all

.PHONY: all clean run host test test-live

all: $(TARGET)

$(TARGET): $(OBJS) | bin
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)
	cp -f $(TARGET) bin/btop

host:
	$(MAKE) -C windows-native

test: all
	mkdir -p bin/tests
	$(CC) $(CFLAGS) -o bin/tests/bridge tests/bridge.c src/cJSON.c -lm
	./bin/tests/bridge
	$(CC) $(CFLAGS) -o bin/tests/tui tests/tui.c -lutil
	./bin/tests/tui

test-live: all host
	mkdir -p bin/tests
	$(CC) $(CFLAGS) -o bin/tests/stream tests/stream.c src/cJSON.c -lm
	./windows-native/publish/sysmon-host.exe --frames 3 | ./bin/tests/stream

bin:
	mkdir -p bin

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./wintop

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET) bin/btop
