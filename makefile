CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= /usr/local
BIN     = limpacache

# Dependencias de compilacao:
#   Linux Mint / Ubuntu / Debian : sudo apt install build-essential
#   Fedora                       : sudo dnf install gcc make
#   Arch                         : sudo pacman -S base-devel
#   openSUSE                     : sudo zypper install gcc make
#   Windows (cross, a partir do Linux) : mingw-w64  ->  make windows

all: $(BIN)

$(BIN): limpacache.c
	$(CC) $(CFLAGS) -o $@ $<

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

windows: limpacache.c
	x86_64-w64-mingw32-gcc -O2 -Wall -o $(BIN).exe $< -lpsapi

clean:
	rm -f $(BIN) $(BIN).exe

.PHONY: all install uninstall windows clean
