CFLAGS=-O0 -g -Wall
LDFLAGS=-lev

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o )

all: $(OBJS)
	$(CC) $(LDFLAGS) -o statsrelay $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o statsrelay
