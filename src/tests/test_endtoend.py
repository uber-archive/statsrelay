#!/usr/bin/env python

import os
import signal
import socket
import subprocess
import sys
import time
import types
import unittest

from collections import defaultdict


class TestCase(unittest.TestCase):

    def launch_process(self, *extra_args):
        args = ['./statsrelay', '--verbose'] + list(extra_args)
        proc = subprocess.Popen(args)
        time.sleep(0.1)
        return proc

    def reload_process(self, proc):
        proc.send_signal(signal.SIGHUP)
        time.sleep(0.1)

    def check_recv(self, fd, expected, size=1024):
        bytes_read = fd.recv(size)
        self.assertEqual(bytes_read, expected)


class ConfigTestCase(TestCase):

    def test_bind(self):
        """Test different parsings for the --bind option."""
        proc = None
        for host_port_str in ('*:8125',
                              '127.0.0.1:8125',
                              '0.0.0.0:8125'):
            try:
                proc = self.launch_process('--config=tests/statsrelay_tcp.conf',
                                           '--bind=' + host_port_str)
                s = socket.socket()
                s.connect(('127.0.0.1', 8125))
            finally:
                if proc is not None:
                    proc.terminate()

    def test_invalid_config_file(self):
        """Test that directories are correctly ignored as config files."""
        proc = self.launch_process('--config=.')
        proc.wait()
        self.assertEqual(proc.returncode, 1)


class StatsdTestCase(TestCase):

    def test_tcp_listener(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', 8126))
        listener.listen(1)

        proc = self.launch_process('--config=tests/statsrelay_tcp.conf',
                                   '--bind=127.0.0.1:8125')
        self.reload_process(proc)

        try:
            sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sender.connect(('127.0.0.1', 8125))
            sender.sendall('test:1|c\n')
            fd, addr = listener.accept()
            self.check_recv(fd, 'test:1|c\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\n')
            fd.close()
            print 'Wait for backoff timeout...'
            time.sleep(6.0)
            sender.sendall('test:xxx\n')
            sender.sendall('test:1|c\n')
            fd, addr = listener.accept()
            self.check_recv(fd, 'test:1|c\n')
            sender.close()

            sender = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sender.connect(('127.0.0.1', 8125))
            sender.sendall('tcptest:1|c\n')
            self.check_recv(fd, 'tcptest:1|c\n')

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
            self.assertEqual(backends['127.0.0.1:8126']['relayed_lines'], 4)
            self.assertEqual(backends['127.0.0.1:8126']['dropped_lines'], 0)
            self.assertEqual(backends['127.0.0.1:8126']['bytes_queued'],
                             backends['127.0.0.1:8126']['bytes_sent'])

            fd.close()
        finally:
            proc.terminate()
            listener.close()

    def test_udp_listener(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', 8126))

        proc = self.launch_process('--config=tests/statsrelay_udp.conf',
                                   '--bind=127.0.0.1:8125')
        self.reload_process(proc)

        try:
            sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sender.connect(('127.0.0.1', 8125))
            sender.sendall('test:1|c\n')
            fd = listener
            self.check_recv(fd, 'test:1|c\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\n')
            sender.sendall('test:xxx\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\n')
            sender.close()

            sender = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sender.connect(('127.0.0.1', 8125))
            sender.sendall('tcptest:1|c\n')
            self.check_recv(fd, 'tcptest:1|c\n')

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
            self.assertEqual(backends['127.0.0.1:8126']['relayed_lines'], 4)
            self.assertEqual(backends['127.0.0.1:8126']['dropped_lines'], 0)
            self.assertEqual(backends['127.0.0.1:8126']['bytes_queued'],
                             backends['127.0.0.1:8126']['bytes_sent'])

            fd.close()
        finally:
            proc.terminate()
            listener.close()


class CarbonTestCase(TestCase):
    def test_carbon(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', 8126))

        proc = self.launch_process('--protocol=carbon',
                                   '--config=tests/statsrelay_udp.conf',
                                   '--bind=127.0.0.1:2003')

        try:
            fd = listener
            sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sender.connect(('127.0.0.1', 2003))
            sender.sendall('1 2 3\n')
            self.check_recv(fd, '1 2 3\n')
            sender.sendall('4 5 6\n')
            self.check_recv(fd, '4 5 6\n')
            sender.sendall('\n')           # invalid
            sender.sendall('1\n')          # invalid
            sender.sendall('1 2\n')        # invalid
            sender.sendall('a b c\n')
            self.check_recv(fd, 'a b c\n')
            sender.sendall('1 2 3 4\n')    # invalid
            sender.sendall('1 2 3 4 5\n')  # invalid
            sender.sendall('d e f\n')
            self.check_recv(fd, 'd e f\n')
            sender.close()

            sender = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sender.connect(('127.0.0.1', 2003))
            sender.sendall('1 2 3\n')
            self.check_recv(fd, '1 2 3\n')

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
            self.assertEqual(backends['127.0.0.1:8126']['relayed_lines'], 5)
            self.assertEqual(backends['127.0.0.1:8126']['dropped_lines'], 0)
            self.assertEqual(backends['127.0.0.1:8126']['bytes_queued'],
                             backends['127.0.0.1:8126']['bytes_sent'])

            fd.close()
        finally:
            proc.terminate()
            listener.close()
        return 0


def main():
    unittest.main()


if __name__ == '__main__':
    sys.exit(main())
