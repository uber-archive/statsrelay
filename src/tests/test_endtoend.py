#!/usr/bin/env python

import argparse
import subprocess
import socket
import time
import types
import sys

from collections import defaultdict


def check_recv(fd, expected, size=1024):
    bytes_read = fd.recv(size)
    assert bytes_read == expected, 'expected %r, actually read %r' % (
        expected, bytes_read)


def test_tcp(run_process):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('127.0.0.1', 8126))
    listener.listen(1)

    if run_process:
        proc = subprocess.Popen(['./statsrelay',
                                 '--verbose',
                                 '--config=tests/statsrelay_tcp.conf',
                                 '--bind=127.0.0.1:8125'])
        time.sleep(0.1)
        proc.send_signal(1)
        time.sleep(0.1)

    try:
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sender.connect(('127.0.0.1', 8125))
        sender.sendall('test:1|c\n')
        fd, addr = listener.accept()
        check_recv(fd, 'test:1|c\n')
        sender.sendall('test:1|c\n')
        check_recv(fd, 'test:1|c\n')
        fd.close()
        print 'Wait for backoff timeout...'
        time.sleep(6.0)
        sender.sendall('test:xxx\n')
        sender.sendall('test:1|c\n')
        fd, addr = listener.accept()
        assert fd.recv(1024) == 'test:1|c\n'
        sender.close()

        sender = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sender.connect(('127.0.0.1', 8125))
        sender.sendall('tcptest:1|c\n')
        assert fd.recv(1024) == 'tcptest:1|c\n'

        sender.sendall('status\n')
        status = sender.recv(65536)
        sender.close()

        backends = defaultdict(dict)
        for line in status.split('\n'):
            if not line:
                break
            if not line.startswith('backend:'):
                continue
            backend, key, valuetype, value = line.split(' ', 3)
            backend = backend.split(':', 1)[1]
            backends[backend][key] = int(value)
        assert backends['127.0.0.1:8126']['relayed_lines'] == 4
        assert backends['127.0.0.1:8126']['dropped_lines'] == 0
        assert backends['127.0.0.1:8126']['bytes_queued'] == backends['127.0.0.1:8126']['bytes_sent']

        fd.close()
    finally:
        if run_process:
            proc.terminate()
        listener.close()
    return 0


def test_udp(run_process):
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('127.0.0.1', 8126))

    if run_process:
        proc = subprocess.Popen(['./statsrelay',
                                 '--verbose',
                                 '--config=tests/statsrelay_udp.conf',
                                 '--bind=127.0.0.1:8125'])
        time.sleep(0.1)
        proc.send_signal(1)
        time.sleep(0.1)

    try:
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sender.connect(('127.0.0.1', 8125))
        sender.sendall('test:1|c\n')
        fd = listener
        check_recv(fd, 'test:1|c\n')
        sender.sendall('test:1|c\n')
        check_recv(fd, 'test:1|c\n')
        sender.sendall('test:xxx\n')
        sender.sendall('test:1|c\n')
        check_recv(fd, 'test:1|c\n')
        sender.close()

        sender = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sender.connect(('127.0.0.1', 8125))
        sender.sendall('tcptest:1|c\n')
        check_recv(fd, 'tcptest:1|c\n')

        sender.sendall('status\n')
        status = sender.recv(65536)
        sender.close()

        backends = defaultdict(dict)
        for line in status.split('\n'):
            if not line:
                break
            if not line.startswith('backend:'):
                continue
            backend, key, valuetype, value = line.split(' ', 3)
            backend = backend.split(':', 1)[1]
            backends[backend][key] = int(value)
        assert backends['127.0.0.1:8126']['relayed_lines'] == 4
        assert backends['127.0.0.1:8126']['dropped_lines'] == 0
        assert backends['127.0.0.1:8126']['bytes_queued'] == backends['127.0.0.1:8126']['bytes_sent']

        fd.close()
    finally:
        if run_process:
            proc.terminate()
        listener.close()
    return 0


def test_carbon(run_process):
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('127.0.0.1', 8126))

    if run_process:
        proc = subprocess.Popen(['./statsrelay',
                                 '--protocol=carbon',
                                 '--verbose',
                                 '--config=tests/statsrelay_udp.conf',
                                 '--bind=127.0.0.1:2003'])
        time.sleep(0.1)
        proc.send_signal(1)
        time.sleep(0.1)

    try:
        fd = listener
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sender.connect(('127.0.0.1', 2003))
        sender.sendall('1 2 3\n')
        check_recv(fd, '1 2 3\n')
        sender.sendall('4 5 6\n')
        check_recv(fd, '4 5 6\n')
        sender.sendall('\n')           # invalid
        sender.sendall('1\n')          # invalid
        sender.sendall('1 2\n')        # invalid
        sender.sendall('a b c\n')
        check_recv(fd, 'a b c\n')
        sender.sendall('1 2 3 4\n')    # invalid
        sender.sendall('1 2 3 4 5\n')  # invalid
        sender.sendall('d e f\n')
        check_recv(fd, 'd e f\n')
        sender.close()

        sender = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sender.connect(('127.0.0.1', 2003))
        sender.sendall('1 2 3\n')
        check_recv(fd, '1 2 3\n')

        sender.sendall('status\n')
        status = sender.recv(65536)
        sender.close()

        backends = defaultdict(dict)
        for line in status.split('\n'):
            if not line:
                break
            if not line.startswith('backend:'):
                continue
            backend, key, valuetype, value = line.split(' ', 3)
            backend = backend.split(':', 1)[1]
            backends[backend][key] = int(value)
        assert backends['127.0.0.1:8126']['relayed_lines'] == 5
        assert backends['127.0.0.1:8126']['dropped_lines'] == 0
        assert backends['127.0.0.1:8126']['bytes_queued'] == backends['127.0.0.1:8126']['bytes_sent']

        fd.close()
    finally:
        if run_process:
            proc.terminate()
        listener.close()
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--gdb-mode', action='store_true',
                        help="Don't launch statsrelay processes, allow it to be "
                        "launched externally; this is useful for running with "
                        "gdb or valgrind")
    parser.add_argument('--run-test', nargs='+', help='Only run this test (accumulates)')
    args = parser.parse_args()

    test_funcs = []
    for k, v in globals().iteritems():
        if isinstance(v, types.FunctionType) and k.startswith('test_'):
            test_funcs.append((k, v))

    run_process = not args.gdb_mode

    runnable = frozenset('test_' + name for name in (args.run_test or []))

    passed = 0
    failed = 0
    for test_name, test in sorted(test_funcs):
        if not runnable or test_name in runnable:
            ret = test(run_process)
            if ret != 0:
                print '%s: fail' % (test_name,)
                failed += 1
            else:
                print '%s: success' % (test_name,)
                passed += 1

    print '%d passed, %d failed' % (passed, failed)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
