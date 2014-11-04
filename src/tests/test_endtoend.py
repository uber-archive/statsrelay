#!/usr/bin/env python
from collections import defaultdict
import subprocess
import socket
import time
import sys


def main():
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('127.0.0.1', 8126))
    listener.listen(1)

    proc = subprocess.Popen(['./statsrelay', '--verbose', '--config=tests/statsrelay.conf', '--bind=127.0.0.1:8125'])
    time.sleep(0.1)
    proc.send_signal(1)
    time.sleep(0.1)

    try:
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sender.connect(('127.0.0.1', 8125))
        sender.sendall('test:1|c\n')
        fd, addr = listener.accept()
        assert fd.recv(1024) == 'test:1|c\n'
        sender.sendall('test:1|c\n')
        assert fd.recv(1024) == 'test:1|c\n'
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
        proc.terminate()
        listener.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
