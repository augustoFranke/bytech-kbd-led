CC      ?= cc
PKGCFG  := $(shell pkg-config --cflags --libs hidapi)
CFLAGS  ?= -Wall -Wextra -Werror -O2
FRAMEWORKS := -framework IOKit -framework CoreFoundation

.PHONY: all clean

all: kbdled

kbdled: kbdled.c
	$(CC) $(CFLAGS) -o $@ $< $(PKGCFG) $(FRAMEWORKS)

clean:
	rm -f kbdled
