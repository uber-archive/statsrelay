import logging
import os
import random
import shutil
import socket
import subprocess
import tempfile
import unittest
import StringIO

import carbonsink


class TestCarbonMetric(unittest.TestCase):

    def test_simple(self):
        metric = carbonsink.CarbonMetric.from_statsite('foo|bar|1')
        self.assertEqual(metric.key, 'foo')
        self.assertEqual(metric.value, 'bar')
        self.assertEqual(metric.timestamp, 1)
        self.assertEqual(metric.as_carbon_line(), 'foo bar 1\n')

    def test_spaces(self):
        metric = carbonsink.CarbonMetric.from_statsite('foo quux|bar|1')
        self.assertEqual(metric.key, 'foo_quux')  # space is transformed to _
        self.assertEqual(metric.value, 'bar')
        self.assertEqual(metric.timestamp, 1)


class TestFileMonitoringSink(unittest.TestCase):

    def setUp(self):
        self.tempfile = tempfile.NamedTemporaryFile()

    def check_contents(self, expected):
        with open(self.tempfile.name) as tf:
            contents = tf.read()
        self.assertEqual(contents, expected)

    def test_file_monitoring_sink(self):
        self.check_contents('')
        sink = carbonsink.FileMonitoringSink(self.tempfile.name, 'foo')
        sink.write(carbonsink.CarbonMetric.from_statsite('wrong|0|1'))
        self.check_contents('')
        sink.write(carbonsink.CarbonMetric.from_statsite('foo|2|3'))
        self.check_contents('2\n')
        sink.write(carbonsink.CarbonMetric.from_statsite('foo|4|5'))
        self.check_contents('4\n')


class ListenerTestCase(unittest.TestCase):
    """Allow tests to run that listen on a port.

    This is kind of a hack. The listener is created using nc with the
    subset of options that seem to be portable across the different nc
    implementations.
    """

    def setUp(self):
        super(ListenerTestCase, self).setUp()
        sock = socket.socket()
        sock.bind(('127.0.0.1', 0))
        _, port = sock.getsockname()
        sock.close()
        self.proc = subprocess.Popen(
            ['nc', '-l', '-p', str(port)],
            stdout=subprocess.PIPE)
        self.port = port

    def tearDown(self):
        self.proc.kill()
        super(ListenerTestCase, self).tearDown()


class TestCarbonSink(ListenerTestCase):

    def test_carbon_sink(self):
        sink = carbonsink.CarbonSink('127.0.0.1', self.port)
        metric = carbonsink.CarbonMetric.from_statsite('foo|bar|1')
        sink.write(metric)
        self.assertEqual(self.proc.stdout.readline(), 'foo bar 1\n')


class TestMetricHandler(ListenerTestCase):

    def setUp(self):
        super(TestMetricHandler, self).setUp()
        self.hasher = None
        self.addr = '127.0.0.1:%d' % (self.port,)
        self.tempfile = tempfile.NamedTemporaryFile()
        self.tempd = None

    def tearDown(self):
        if self.hasher is not None:
            self.hasher.kill()
        if self.tempd is not None:
            shutil.rmtree(self.tempd)
        super(TestMetricHandler, self).tearDown()

    def check_monitoring_contents(self, expected):
        with open(self.tempfile.name) as tf:
            contents = tf.read()
        self.assertEqual(contents, expected)

    def check_line(self, expected):
        data = self.proc.stdout.readline()
        self.assertEqual(data, expected + '\n')

    def test_no_buffering(self):
        with carbonsink.metric_handler([self.addr], 'prefix') as handler:
            self.check_monitoring_contents('')
            handler.add_monitoring_sink(self.tempfile.name, 'mon')
            self.check_monitoring_contents('')

            # handle a simple metric
            handler.handle_metric('foo|bar|1')
            self.check_monitoring_contents('')
            self.check_line('prefix.foo bar 1')

            # test the monitoring metric
            handler.handle_metric('mon|foo|1')
            self.check_monitoring_contents('foo\n')
            self.check_line('prefix.mon foo 1')

            # test again without the monitoring metric
            handler.handle_metric('quux|baz|1')
            self.check_monitoring_contents('foo\n')
            self.check_line('prefix.quux baz 1')

    def test_with_buffering(self):
        fruits = ['apple', 'banana', 'cherry', 'durian', 'guava', 'kiwi',
                  'lemon', 'orange', 'peach', 'pear', 'quince', 'strawberry']

        stathasher_file = tempfile.NamedTemporaryFile()
        stathasher_file.write("carbon:\n  bind: 127.0.0.1:2004\n  "
                              "shard_map: {0: '127.0.0.1:2000', "
                              "1: '127.0.0.1:2000', "
                              "2: '127.0.0.1:2000', "
                              "3: '127.0.0.1:2000'}\n")
        stathasher_file.flush()

        self.hasher = subprocess.Popen(
            [carbonsink.MetricHandler.STATHASHER_PATH,
             '-c', stathasher_file.name],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE)

        fruit_hash = {}
        for fruit in fruits:
            fruit_key = '%s.%s' % ('prefix', fruit)
            fruit_hash[fruit] = carbonsink.get_hash(
                self.hasher, fruit_key)['carbon_shard']

        # ensure that all shards are accounted for
        self.assertEqual(set(fruit_hash.values()), set([0, 1, 2, 3]))

        # pick two random shards to buffer
        buffer_shards = random.sample([0, 1, 2, 3], 2)
        buffer_file = StringIO.StringIO('\n'.join(map(str, buffer_shards)))

        self.tempd = tempfile.mkdtemp()

        with carbonsink.metric_handler([self.addr], 'prefix') as handler:
            handler.detect_buffer_shards(
                buffer_file, self.tempd, stathasher_file.name)
            for fruit in fruits:
                carbon_line = 'prefix.%s foo 1' % (fruit,)
                shard_num = fruit_hash[fruit]
                handler.handle_metric('%s|foo|1' % (fruit,))
                if shard_num not in buffer_shards:
                    # check that the nc process received the data
                    self.check_line(carbon_line)
                else:
                    # open the buffer file and get the last line
                    filename = os.path.join(
                        self.tempd, 'shard_%d.txt' % (shard_num,))
                    with open(filename) as log_file:
                        for line in log_file:
                            pass
                    # ensure the final line is the buffered line
                    self.assertEqual(carbon_line + '\n', line)


if __name__ == '__main__':
    logging.basicConfig()
    logging.root.setLevel(logging.ERROR + 1)
    unittest.main()
