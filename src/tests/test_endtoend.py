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
        #args = ['./statsrelay', '--verbose', '--log-level=DEBUG'] + list(extra_args)
        args = ['./statsrelay', '--verbose'] + list(extra_args)
        if not any(c.startswith('--config') for c in extra_args):
            args.append('--config=tests/statsrelay.yaml')
        proc = subprocess.Popen(args)
        time.sleep(0.5)
        return proc

    def reload_process(self, proc):
        proc.send_signal(signal.SIGHUP)
        time.sleep(0.1)

    def check_recv(self, fd, expected, size=1024):
        bytes_read = fd.recv(size)
        self.assertEqual(bytes_read, expected)

    def recv_status(self, fd):
        output = ''
        return fd.recv(65536)


class ConfigTestCase(TestCase):

    def test_invalid_config_file(self):
        """Test that directories are correctly ignored as config files."""
        proc = self.launch_process('--config=.')
        proc.wait()
        self.assertEqual(proc.returncode, 1)

        proc = self.launch_process('--config=/etc/passwd')
        proc.wait()
        self.assertEqual(proc.returncode, 1)

class StatsdTestCase(TestCase):

    def test_tcp_listener(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', 8126))
        listener.listen(1)

        proc = self.launch_process()

        try:
            fd, addr = listener.accept()
            sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sender.connect(('127.0.0.1', 8125))
            sender.sendall('test:1|c\n')
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
            self.assertEqual(backends['127.0.0.1:8126:tcp']['relayed_lines'], 4)
            self.assertEqual(backends['127.0.0.1:8126:tcp']['dropped_lines'], 0)
            self.assertEqual(backends['127.0.0.1:8126:tcp']['bytes_queued'],
                             backends['127.0.0.1:8126:tcp']['bytes_sent'])

            fd.close()
        finally:
            proc.terminate()
            listener.close()

    def test_udp_listener(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', 8126))

        proc = self.launch_process('--config=tests/statsrelay_udp.yaml')
        #self.reload_process(proc)

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
            self.assertEqual(backends['127.0.0.1:8126:udp']['relayed_lines'], 4)
            self.assertEqual(backends['127.0.0.1:8126:udp']['dropped_lines'], 0)
            self.assertEqual(backends['127.0.0.1:8126:udp']['bytes_queued'],
                             backends['127.0.0.1:8126:udp']['bytes_sent'])

            fd.close()
        finally:
            proc.terminate()
            listener.close()


class CarbonTestCase(TestCase):

    def run_checks(self, fd, sender):
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
        sender.sendall('1 2 3\n')
        self.check_recv(fd, '1 2 3\n')


        sender = socket.socket()
        sender.connect(('127.0.0.1', 2003))
        sender.sendall('status\n')
        status = self.recv_status(sender)
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
        return dict(backends)

    def test_carbon_tcp(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', 2004))
        listener.listen(1)

        proc = self.launch_process()

        try:
            fd, addr = listener.accept()
            sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            backends = self.run_checks(fd, sender)
            self.assertEqual(backends['127.0.0.1:2004:tcp']['relayed_lines'], 5)
            self.assertEqual(backends['127.0.0.1:2004:tcp']['dropped_lines'], 0)
            self.assertEqual(backends['127.0.0.1:2004:tcp']['bytes_queued'],
                             backends['127.0.0.1:2004:tcp']['bytes_sent'])

            fd.close()
        finally:
            proc.terminate()
            listener.close()

    def test_carbon_udp(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(('127.0.0.1', 2004))

        proc = self.launch_process('--config=tests/statsrelay_udp.yaml')

        try:
            sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            backends = self.run_checks(listener, sender)
            self.assertEqual(backends['127.0.0.1:2004:udp']['relayed_lines'], 5)
            self.assertEqual(backends['127.0.0.1:2004:udp']['dropped_lines'], 0)
            self.assertEqual(backends['127.0.0.1:2004:udp']['bytes_queued'],
                             backends['127.0.0.1:2004:udp']['bytes_sent'])

        finally:
            proc.terminate()
            listener.close()
        return 0


def main():
    unittest.main()


if __name__ == '__main__':
    sys.exit(main())
