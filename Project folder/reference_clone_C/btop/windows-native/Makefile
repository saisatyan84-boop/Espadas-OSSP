CC = x86_64-w64-mingw32-gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-cast-function-type -I../include
LDLIBS = -lsetupapi -lbthprops -liphlpapi -lws2_32 -lpsapi -ladvapi32 -lpdh -lole32 -luuid
SOURCES = main.c system.c network.c devices.c displays.c audio.c storage.c sensors.c cpu_sensors.c ../src/cJSON.c
all: publish/sysmon-host.exe
publish/sysmon-host.exe: $(SOURCES) host.h
	mkdir -p publish
	$(CC) $(CFLAGS) $(SOURCES) -o $@ $(LDLIBS) -static-libgcc
	chmod +x $@
.PHONY: all
