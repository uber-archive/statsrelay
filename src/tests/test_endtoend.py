#!/usr/bin/env python

import contextlib
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest

from collections import defaultdict


SOCKET_TIMEOUT = 1

# While debugging, set this to False
QUIET = True
if QUIET:
    DEVNULL = open('/dev/null', 'wb')
    POPEN_KW = {'stdout': DEVNULL, 'stderr': DEVNULL}
else:
    POPEN_KW = {}


class TestCase(unittest.TestCase):

    def setUp(self):
        super(TestCase, self).setUp()
        self.tcp_cork = 'false'
        self.proc = None

    def tearDown(self):
        super(TestCase, self).tearDown()
        if self.proc:
            try:
                self.proc.kill()
            except OSError:
                pass

    def launch_process(self, config_path):
        args = ['./statsrelay', '--verbose', '--log-level=DEBUG']
        args.append('--config=' + config_path)
        self.proc = subprocess.Popen(args, **POPEN_KW)
        time.sleep(0.5)

    def reload_process(self, proc):
        proc.send_signal(signal.SIGHUP)
        time.sleep(0.1)

    def check_recv(self, fd, expected, size=1024):
        bytes_read = fd.recv(size)
        self.assertEqual(bytes_read, expected)

    def recv_status(self, fd):
        return fd.recv(65536)

    @contextlib.contextmanager
    def generate_config(self, mode):
        if mode.lower() == 'tcp':
            sock_type = socket.SOCK_STREAM
            config_path = 'tests/statsrelay.yaml'
        elif mode.lower() == 'udp':
            sock_type = socket.SOCK_DGRAM
            config_path = 'tests/statsrelay_udp.yaml'
        else:
            raise ValueError()

        try:
            self.bind_carbon_port = self.choose_port(sock_type)
            self.bind_statsd_port = self.choose_port(sock_type)

            self.carbon_listener = socket.socket(socket.AF_INET, sock_type)
            self.carbon_listener.bind(('127.0.0.1', 0))
            self.carbon_listener.settimeout(SOCKET_TIMEOUT)
            self.carbon_port = self.carbon_listener.getsockname()[1]

            self.statsd_listener = socket.socket(socket.AF_INET, sock_type)
            self.statsd_listener.bind(('127.0.0.1', 0))
            self.statsd_listener.settimeout(SOCKET_TIMEOUT)
            self.statsd_port = self.statsd_listener.getsockname()[1]

            if mode.lower() == 'tcp':
                self.carbon_listener.listen(1)
                self.statsd_listener.listen(1)

            new_config = tempfile.NamedTemporaryFile()
            with open(config_path) as config_file:
                data = config_file.read()
            for var, replacement in [
                    ('BIND_CARBON_PORT', self.bind_carbon_port),
                    ('BIND_STATSD_PORT', self.bind_statsd_port),
                    ('SEND_CARBON_PORT', self.carbon_port),
                    ('SEND_STATSD_PORT', self.statsd_port),
                    ('TCP_CORK', self.tcp_cork)]:
                data = data.replace(var, str(replacement))
            new_config.write(data)
            new_config.flush()
            yield new_config.name
        finally:
            self.statsd_listener.close()
            self.carbon_listener.close()

    def connect(self, sock_type, port):
        if sock_type.lower() == 'tcp':
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        else:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect(('127.0.0.1', port))
        sock.settimeout(SOCKET_TIMEOUT)
        return sock

    def choose_port(self, sock_type):
        s = socket.socket(socket.AF_INET, sock_type)
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
        s.close()
        return port


class ConfigTestCase(TestCase):
    """Test config option parsing."""

    def test_invalid_config_file(self):
        """Test that directories are correctly ignored as config files."""
        self.launch_process('.')
        self.proc.wait()
        self.assertEqual(self.proc.returncode, 1)

        self.launch_process('/etc/passwd')
        self.proc.wait()
        self.assertEqual(self.proc.returncode, 1)

    def test_check_invalid_config_file(self):
        proc = subprocess.Popen(
            ['./statsrelay', '-t', '/etc/passwd'], **POPEN_KW)
        proc.wait()
        self.assertEqual(proc.returncode, 1)

    def test_check_valid_tcp_file(self):
        with self.generate_config('tcp') as config_path:
            proc = subprocess.Popen(['./statsrelay', '-t', config_path])
            proc.wait()
            self.assertEqual(proc.returncode, 0)

    def test_check_valid_udp_file(self):
        with self.generate_config('udp') as config_path:
            proc = subprocess.Popen(['./statsrelay', '-t', config_path])
            proc.wait()
            self.assertEqual(proc.returncode, 0)

    def test_check_empty_file(self):
        proc = subprocess.Popen(['./statsrelay', '-c', 'tests/empty.yaml'])
        proc.wait()
        self.assertEqual(proc.returncode, 1)


class StatsdTestCase(TestCase):

    def test_tcp_listener(self):
        with self.generate_config('tcp') as config_path:
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('udp', self.bind_statsd_port)
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\n')
            fd.close()
            time.sleep(6.0)
            sender.sendall('test:xxx\n')
            sender.sendall('test:1|c\n')
            fd, addr = self.statsd_listener.accept()
            self.check_recv(fd, 'test:1|c\n')
            sender.close()

            sender = self.connect('tcp', self.bind_statsd_port)
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

            key = '127.0.0.1:%d:tcp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 4)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(backends[key]['bytes_queued'],
                             backends[key]['bytes_sent'])

    def test_udp_listener(self):
        with self.generate_config('udp') as config_path:
            self.launch_process(config_path)
            sender = self.connect('udp', self.bind_statsd_port)
            sender.sendall('test:1|c\n')
            fd = self.statsd_listener
            self.check_recv(fd, 'test:1|c\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\n')
            sender.sendall('test:xxx\n')
            sender.sendall('test:1|c\n')
            self.check_recv(fd, 'test:1|c\n')
            sender.close()

            sender = self.connect('tcp', self.bind_statsd_port)
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
            key = '127.0.0.1:%d:udp' % (self.statsd_listener.getsockname()[1],)
            self.assertEqual(backends[key]['relayed_lines'], 4)
            self.assertEqual(backends[key]['dropped_lines'], 0)
            self.assertEqual(backends[key]['bytes_queued'],
                             backends[key]['bytes_sent'])

    def test_tcp_cork(self):
        if not sys.platform.startswith('linux'):
            return
        if sys.version_info[:2] < (2, 7):
            return
        cork_time = 0.200  # cork time is 200ms on linux
        self.tcp_cork = 'true'
        msg = 'test:1|c\n'
        with self.generate_config('tcp') as config_path:
            self.launch_process(config_path)
            fd, addr = self.statsd_listener.accept()
            sender = self.connect('udp', self.bind_statsd_port)
            t0 = time.time()
            sender.sendall(msg)
            self.check_recv(fd, msg)
            elapsed = time.time() - t0

            # ensure it took about cork_time ms
            self.assertGreater(elapsed, cork_time * 0.95)
            self.assertLess(elapsed, cork_time * 1.25)

            # try sending w/o corking... this assumes the mtu of the
            # loopback interface is 64k. need to send multiple
            # messages to avoid getting EMSGSIZE
            needed_messages = ((1 << 16) / len(msg)) / 2
            buf = msg * needed_messages
            t0 = time.time()
            sender.sendall(buf)
            sender.sendall(buf)
            sender.sendall(msg)
            self.assertEqual(len(fd.recv(1024)), 1024)

            # we can tell data wasn't corked because we get a response
            # fast enough
            elapsed = time.time() - t0
            self.assertLess(elapsed, cork_time / 2)

    def test_invalid_line_for_pull_request_35(self):
        with self.generate_config('udp') as config_path:
            self.launch_process(config_path)
            sender = self.connect('udp', self.bind_statsd_port)
            sender.sendall('foo.bar:undefined|quux.quuxly.200:1c\n')
            sender.sendall('foo.bar:1|c\n')
            fd = self.statsd_listener
            self.check_recv(fd, 'foo.bar:1|c\n')
            self.assert_(self.proc.returncode is None)


class CarbonTestCase(TestCase):

    def run_checks(self, fd, proto):
        sender = self.connect('udp', self.bind_carbon_port)
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

        sender = self.connect('tcp', self.bind_carbon_port)
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

        key = '127.0.0.1:%d:%s' % (
            self.carbon_listener.getsockname()[1], proto)
        self.assertEqual(backends[key]['relayed_lines'], 5)
        self.assertEqual(backends[key]['dropped_lines'], 0)
        self.assertEqual(backends[key]['bytes_queued'],
                         backends[key]['bytes_sent'])

    def test_carbon_tcp(self):
        with self.generate_config('tcp') as config:
            self.launch_process(config)
            fd, addr = self.carbon_listener.accept()
            self.run_checks(fd, 'tcp')

    def test_carbon_udp(self):
        with self.generate_config('udp') as config:
            self.launch_process(config)
            self.run_checks(self.carbon_listener, 'udp')


class StathasherTests(unittest.TestCase):

    def get_foo(self, config):
        proc = subprocess.Popen(['./stathasher', '-c', config],
                                stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE)
        proc.stdin.write('foo\n')
        line = proc.stdout.readline()
        return line

    def test_stathasher(self):
        line = self.get_foo('tests/stathasher.yaml')
        self.assertEqual(line, 'key=foo carbon=127.0.0.1:2001 carbon_shard=1 statsd=127.0.0.1:3001 statsd_shard=1\n')  # noqa

    def test_stathasher_empty(self):
        line = self.get_foo('tests/empty.yaml')
        self.assertEqual(line, 'key=foo\n')

    def test_stathasher_just_carbon(self):
        line = self.get_foo('tests/stathasher_just_carbon.yaml')
        self.assertEqual(line, 'key=foo carbon=127.0.0.1:2001 carbon_shard=1\n')

    def test_stathasher_just_statsd(self):
        line = self.get_foo('tests/stathasher_just_statsd.yaml')
        self.assertEqual(line, 'key=foo statsd=127.0.0.1:3001 statsd_shard=1\n')


def main():
    unittest.main()


if __name__ == '__main__':
    sys.exit(main())
