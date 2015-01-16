"""A sink for statsite to send metrics to carbon.

This supports a few interesting features:

 * There's support for a "file monitoring" sink which works by
   detecting specially formatted metrics, and updating the mtime of a
   file on the local filesystem; this facilitates monitoring.

 * Support for buffering metrics, which is useful when moving them
   with statsrelay.
"""

import argparse
import contextlib
import logging
import os
import socket
import subprocess
import sys
import time


log = logging.getLogger('carbonsink')


def get_hash(hasher, key):
    hasher.stdin.write(key + '\n')
    line = hasher.stdout.readline()
    out_dict = {}
    for part in line.split():
        k, v = part.split('=', 1)
        if k.endswith('_shard'):
            v = int(v, 10)
        out_dict[k] = v
    return out_dict


class CarbonMetric(object):
    """Representation of a metric in carbon."""

    __slots__ = ['key', 'value', 'timestamp']

    def __init__(self, key, value, timestamp):
        self.key = key.replace(' ', '_')
        self.value = value
        self.timestamp = int(timestamp, 10)

    @classmethod
    def from_statsite(cls, line):
        key, val, timestamp = line.split('|', 2)
        return cls(key, val, timestamp)

    def as_carbon_line(self):
        return '%s %s %d\n' % (self.key, self.value, self.timestamp)


class Sink(object):
    """Abstraction for sinks."""

    def write(self, metric):
        raise NotImplemented


class CarbonSink(Sink):
    """A sink which writes to a carbon-cache or carbon-relay daemon."""

    def __init__(self, host, port, retries=3):
        self.host = host
        self.port = int(port)
        self.retries = retries
        self.sock = None

    def connect(self):
        if self.sock is None:
            log.info('Connecting to %s:%s', self.host, self.port)
            self.sock = socket.create_connection(
                (self.host, self.port), timeout=10)

    def write(self, metric):
        """Write the metric, retrying if necessary."""
        for attempt in xrange(self.retries):
            carbon_line = metric.as_carbon_line()
            try:
                self.connect()
                self.sock.sendall(carbon_line)
                return True
            except:
                log.exception('Failed to send metrics to %s:%s on attempt %d',
                              self.host, self.port, attempt)
                self.close()
                time.sleep(0.1)
        log.error('Dropping metric after %d retries', self.retries)
        return False

    def close(self):
        """Ensure the underlying socket is closed."""
        if self.sock is not None:
            try:
                self.sock.close()
            except:
                pass
            self.sock = None


class FileMonitoringSink(Sink):
    """A sink that writes timestamps for specially formatted stats.

    The purpose of this sink is to facilitate monitoring (e.g. from
    nagios checks).
    """

    def __init__(self, filename, monitoring_stat):
        self.filename = filename
        self.monitoring_stat = monitoring_stat

    def write(self, metric):
        if metric.key == self.monitoring_stat:
            try:
                with open(self.filename, 'w') as f:
                    f.write('%s\n' % (metric.value,))
            except IOError:
                log.exception('Failed to write to filename %s', self.filename)
                return False
        return True


class MetricHandler(object):
    """Handler for metrics.

    This class encapsulates the logic for knowing when to buffer
    stats, how to write to multiple sinks, and so forth.
    """

    STATHASHER_PATH = '/usr/bin/stathasher'

    def __init__(self, servers, prefix):
        servers = [x.rsplit(':', 1) for x in servers]
        self.sinks = [CarbonSink(*x) for x in servers]
        self.prefix = prefix
        self.buffer_shards = set()
        self.buffer_dir = None
        self.buffer_cache = {}
        self.stathasher = None

    def add_monitoring_sink(self, filename, metric_to_monitor):
        metric_to_monitor = '%s.%s' % (self.prefix, metric_to_monitor)
        self.sinks.append(FileMonitoringSink(filename, metric_to_monitor))

    def detect_buffer_shards(self, fileobj, buffer_dir,
                             stathasher_config=None):
        """Populate self.buffer_shards from the buffer file."""
        self.buffer_dir = buffer_dir
        try:
            for line in fileobj:
                try:
                    shard_num = int(line, 10)
                except ValueError:
                    pass
                self.buffer_shards.add(shard_num)
        except IOError:
            # if we can't open the file (e.g. it doesn't exist), then
            # there are no buffer shards to exclude
            pass

        if self.buffer_shards:
            args = [self.STATHASHER_PATH]
            if stathasher_config is not None:
                args.extend(['-c', stathasher_config])
            self.stathasher = subprocess.Popen(
                args, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def lookup_buffer(self, shard_num):
        """Get the buffer file for a shard num, for appending to."""
        try:
            return self.buffer_cache[shard_num]
        except KeyError:
            filename = os.path.join(
                self.buffer_dir, 'shard_%s.txt' % (shard_num,))
            file_obj = open(filename, 'a')
            self.buffer_cache[shard_num] = file_obj
            return file_obj

    def close(self):
        """Ensure the stathasher process terminates."""
        if self.stathasher and self.stathasher.returncode is None:
            self.stathasher.kill()
        for v in self.buffer_cache.itervalues():
            v.close()

    def buffer_line(self, line, metric):
        """Buffer the line if buffering is enabled for this metric.

        Returns True if the line is buffered, False otherwise.
        """
        if not self.buffer_shards:
            return False
        hashval = get_hash(self.stathasher, metric.key)
        shard = hashval['carbon_shard']
        if shard in self.buffer_shards:
            file_obj = self.lookup_buffer(shard)
            file_obj.write(metric.as_carbon_line())
            file_obj.flush()
            return True
        else:
            return False

    def handle_metric(self, line):
        key, val, timestamp = line.split('|', 2)
        metric = CarbonMetric(self.prefix + '.' + key, val, timestamp)
        if not self.buffer_line(line, metric):
            # The above call will buffer the line if buffering is
            # enabled and suitable for the metric; in the typical
            # case, no buffering will happen, and instead we write the
            # metric to all of the sinks.
            for sink in self.sinks:
                if not sink.write(metric):
                    log.error(
                        'Hard failure sending to %r, giving up on it', sink)
                    continue


@contextlib.contextmanager
def metric_handler(*args, **kwargs):
    with contextlib.closing(MetricHandler(*args, **kwargs)) as handler:
        yield handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--attempts', type=int, default=3)
    parser.add_argument('--logfile', default=None)
    parser.add_argument('--statsite-instance', default=None)
    parser.add_argument('--monitoring-stat', default=None)
    parser.add_argument('-c', '--cache-directory',
                        default='/var/cache/statsite',
                        help='directory for cache files')
    parser.add_argument('-b', '--buffer-shard-file',
                        default='/etc/statsrelay_buffer_shards.txt',
                        help='buffer metrics for shards from this file')
    parser.add_argument('--statsrelay-config',
                        default=None,
                        help='statsrelay config to use when hashing')
    parser.add_argument('prefix')
    parser.add_argument('servers', nargs='+')
    args = parser.parse_args()

    logging.basicConfig()
    if args.logfile:
        fh = logging.FileHandler(args.logfile)
        fh.setFormatter(logging.Formatter(
            fmt='%(asctime)s\t%(levelname)s\t%(message)s'))
        log.addHandler(fh)

    with metric_handler(args.servers, args.prefix) as handler:
        if args.statsite_instance and args.monitoring_stat:
            filename = os.path.join(
                args.cache_directory, args.statsite_instance)
            stat_name = '%s.%s' % (args.prefix, args.monitoring_stat)
            handler.add_monitoring_sink(filename, stat_name)

        try:
            with open(args.buffer_shard_file) as buffer_shard_file:
                handler.detect_buffer_shards(
                    buffer_shard_file,
                    args.cache_directory,
                    args.statsrelay_config)
        except (OSError, IOError):
            pass

        # N.B. we want to force stdin to be read iteratively, to avoid
        # buffering when statsite is sending a large amount of
        # data. This means that we must explicitly get lines using
        # stdin.readline(), and we can't use a map or iterator over
        # stdin.
        while True:
            line = sys.stdin.readline().rstrip()
            if not line:
                break
            handler.handle_metric(line.rstrip())


if __name__ == '__main__':
    main()
