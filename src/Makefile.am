AM_CFLAGS=-O2 -g -std=c99 -pedantic -Wall -fno-strict-aliasing -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -D_BSD_SOURCE
AM_LDFLAGS=-lev -lyaml
bin_PROGRAMS=statsrelay stathasher stresstest
BASE_SOURCES=buffer.c hashlib.c hashring.c list.c log.c protocol.c tcpclient.c tcpserver.c udpserver.c server.c stats.c validate.c yaml_config.c
statsrelay_SOURCES=$(BASE_SOURCES) main.c
stathasher_SOURCES=hashlib.c hashring.c list.c log.c yaml_config.c stathasher.c
stresstest_SOURCES=stresstest.c

check_PROGRAMS=test_hashlib test_hashring
TESTS=$(check_PROGRAMS) tests/test_endtoend.py
#test_stats_SOURCES=tests/test_stats.c buffer.c log.c protocol.c stats.c tcpclient.c validate.c
test_hashlib_SOURCES=tests/test_hashlib.c hashlib.c
test_hashring_SOURCES=tests/test_hashring.c hashlib.c hashring.c list.c log.c
