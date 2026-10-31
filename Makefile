CC ?= cc
CFLAGS ?= -O2

.PHONY: all test
all: spotify-filter.so

spotify-filter.so: filter.c cef_prefix.h
	$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -fPIC -shared -o $@ filter.c -ldl -pthread

test:
	python3 -m unittest discover -s tests -v
