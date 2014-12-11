AM_CFLAGS=-O2 -g -std=c99 -pedantic -Wall -fno-strict-aliasing -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE $(GLIB_CFLAGS)
AM_LDFLAGS=-lcrypto -lev -lm $(GLIB_LDFLAGS)
bin_PROGRAMS=statsrelay stathasher
statsrelay_SOURCES=buffer.c ketama.c log.c protocol.c tcpclient.c tcpserver.c udpserver.c stats.c validate.c main.c
stathasher_SOURCES=buffer.c ketama.c log.c protocol.c tcpclient.c tcpserver.c udpserver.c stats.c validate.c stathasher.c

check_PROGRAMS=test_stats
TESTS=$(check_PROGRAMS) tests/test_endtoend.py
test_stats_SOURCES=tests/test_stats.c buffer.c ketama.c log.c protocol.c stats.c tcpclient.c validate.c
