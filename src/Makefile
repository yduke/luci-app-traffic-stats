#
# Build rules for the traffic-stats collector.
#
# The file is driven by the OpenWrt build system, it is not meant to be
# called manually. The toolchain, the compiler flags and the destination
# directory are provided through the environment.
#

CC := $(CROSS)gcc
INSTALL ?= install

CFLAGS += -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
CFLAGS += -D_GNU_SOURCE
LDLIBS += -pthread

SRCS = main.c capture.c hash.c local.c dns.c store.c json.c util.c
OBJS = $(SRCS:.c=.o)
BINARY = traffic-stats

.PHONY: all compile install clean

all: compile

compile: $(BINARY)

$(BINARY): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c ts.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(BINARY)
	$(INSTALL) -d $(DESTDIR)/usr/libexec/traffic-stats
	$(INSTALL) -m 0755 $(BINARY) $(DESTDIR)/usr/libexec/traffic-stats/traffic-stats

clean:
	rm -f $(OBJS) $(BINARY)
