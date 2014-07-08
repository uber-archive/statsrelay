from collections import defaultdict
import socket
import time
import sys

def get_stats():
    s = socket.socket()
    s.connect(('127.0.0.1', 8125))
    s.sendall('status\n')
    lines = s.recv(65536)
    result = defaultdict(int)
    for line in lines.split('\n'):
        line = line.strip('\r\n\t ')
        if not line:
            continue
        line = line.split(' ')
        result[line[1]] += int(line[3])
    s.close()
    return result

last = get_stats()
interval = float(sys.argv[1])
while True:
    time.sleep(interval)
    stats = get_stats()

    print 'Relayed:  %i/s' % ((stats['relayed_lines'] - last['relayed_lines']) / interval)
    print 'Dropped:  %i' % (stats['dropped_lines'] - last['dropped_lines'])
    print 'In queue: %i bytes' % (stats['bytes_queued'] - stats['bytes_sent'])

    last = stats
