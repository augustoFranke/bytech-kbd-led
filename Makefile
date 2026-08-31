CC      ?= cc
PKGCFG  := $(shell pkg-config --cflags --libs hidapi)
CFLAGS  ?= -Wall -Wextra -Werror -O2

.PHONY: all clean

all: kbdled

kbdled: kbdled.c
	$(CC) $(CFLAGS) -o $@ $< $(PKGCFG)

clean:
	rm -f kbdled
