CFLAGS=-O0 -g -Wall -pedantic -std=c99 -D_XOPEN_SOURCE=600 -D_BSD_SOURCE
LDFLAGS=-lev

CFLAGS += $(shell pkg-config --cflags glib-2.0)
LDFLAGS += $(shell pkg-config --libs glib-2.0)

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o )

all: $(OBJS)
	$(CC) $(LDFLAGS) -o statsrelay $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o statsrelay
