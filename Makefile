DESTDIR:=
PREFIX := /usr/local
bindir:=/bin

#CFLAGS=-O0 -g -Wall -pedantic -std=c99 -D_XOPEN_SOURCE=600 -D_BSD_SOURCE
CFLAGS=-O2 -Wall -pedantic -std=c99 -D_XOPEN_SOURCE=600 -D_BSD_SOURCE

LDFLAGS=-lcrypto -lev -lm

CFLAGS += $(shell pkg-config --cflags glib-2.0)
LDFLAGS += $(shell pkg-config --libs glib-2.0)

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o )

all: $(OBJS)
	$(CC) -o statsrelay $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o statsrelay

install: statsrelay
	install -D -m 0755 statsrelay $(DESTDIR)$(PREFIX)$(bindir)/statsrelay
