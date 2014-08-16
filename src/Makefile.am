CFLAGS=-O2 -std=c99 -pedantic -Wall -fno-strict-aliasing -D_XOPEN_SOURCE=600 -D_BSD_SOURCE $(GLIB_CFLAGS)
LDFLAGS=-lcrypto -lev -lm $(GLIB_LDFLAGS)
bin_PROGRAMS=statsrelay
statsrelay_SOURCES=buffer.c ketama.c log.c tcpclient.c tcpserver.c udpserver.c stats.c main.c
