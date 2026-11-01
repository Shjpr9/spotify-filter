CC ?= cc
CFLAGS ?= -O2

.PHONY: all test
all: spotify-filter.so

spotify-filter.so: filter.c cef_prefix.h
	$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -fPIC -shared -o $@ filter.c -ldl -pthread

test:
	python3 -m unittest discover -s tests -v

# Native MSYS2/MinGW64: make windows WINDOWS_CC=gcc
WINDOWS_CC ?= x86_64-w64-mingw32-gcc
.PHONY: windows
windows: spotify-filter.exe spotify-filter.dll

spotify-filter.exe: windows/launcher.c windows/quote.h
	$(WINDOWS_CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -municode -o $@ windows/launcher.c -lpsapi

spotify-filter.dll: filter.c cef_prefix.h
	$(WINDOWS_CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -shared -o $@ filter.c
