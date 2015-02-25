AM_CFLAGS=-O2 -g -std=c99 -pedantic -Wall -fno-strict-aliasing -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -D_BSD_SOURCE
bin_PROGRAMS=statsrelay stathasher stresstest
BASE_SOURCES=buffer.c hashlib.c hashring.c list.c log.c protocol.c tcpclient.c tcpserver.c udpserver.c server.c stats.c validate.c normalize.c yaml_config.c
statsrelay_SOURCES=$(BASE_SOURCES) main.c
stathasher_SOURCES=hashlib.c hashring.c list.c log.c yaml_config.c stathasher.c
stresstest_SOURCES=stresstest.c

check_PROGRAMS=test_hashlib test_hashring test_normalize
TESTS=$(check_PROGRAMS) tests/test_endtoend.py
test_hashlib_SOURCES=tests/test_hashlib.c hashlib.c
test_hashring_SOURCES=tests/test_hashring.c hashlib.c hashring.c list.c log.c
test_normalize_SOURCES=tests/test_normalize.c normalize.c
