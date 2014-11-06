AM_CFLAGS=-O2 -g -std=c99 -pedantic -Wall -fno-strict-aliasing -D_XOPEN_SOURCE=600 -D_BSD_SOURCE $(GLIB_CFLAGS)
AM_LDFLAGS=-lcrypto -lev -lm $(GLIB_LDFLAGS)
bin_PROGRAMS=statsrelay
statsrelay_SOURCES=buffer.c ketama.c log.c tcpclient.c tcpserver.c udpserver.c stats.c main.c

check_PROGRAMS=test_stats
TESTS=$(check_PROGRAMS) tests/test_endtoend.py
test_stats_SOURCES=tests/test_stats.c buffer.c ketama.c log.c stats.c tcpclient.c
