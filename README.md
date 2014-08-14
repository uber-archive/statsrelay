# statsrelay
A consistent-hashing relay for statsd metrics

[![Build Status](https://travis-ci.org/uber/statsrelay.svg?branch=master)](https://travis-ci.org/uber/statsrelay)
[![Coverity Status](https://scan.coverity.com/projects/2789/badge.svg)](https://scan.coverity.com/projects/2789)

# License
MIT License  
Copyright (c) 2014 Uber Technologies, Inc.

libketama  
BSD License  
Copyright (c) 2007 Last.fm  
Copyright (c) 2007-2014 Richard Jones <rj@metabrew.com>

# Build

Dependencies:
- pkg-config
- libssl (>= 1.0.1e)
- libev (>= 4.11)
- glib (>= 2.32)

```
apt-get install pkg-config libev-dev libglib2.0-dev libssl-dev
make clean
make
make install
```

# Use

Example config:
```
# This is a comment
# Each line is a backend server definition
# IP:PORT WEIGHT
127.0.0.1:8125 600
1.2.3.4:8125 300
# Note: Only dotted quad IPv4 addresses are supported
```

```
statsrelay /path/to/statsrelay.conf
```

This process will run in the foreground. If you need to daemonize, use
start-stop-script, daemontools, supervisord, upstart, systemd, or your
preferred service watchdog.

statsrelay binds to all interfaces on port 8125, tcp and udp.

For each line that statsrelay receives in the statsd format
"statname.foo.bar:1|c\n", the key will be hashed to determine which
backend server the stat will be relayed to. If no connection to that
backend is open, the line is queued and a connection attempt is
started. Once a connection is established, all queued metrics are
relayed to the backend and the queue is emptied. If the backend
connection fails, the queue persists in memory and the connection will
be retried after 5 seconds. Any stats received for that backend during
the retry window are added to the queue.

Each backend has it's own send queue. If a send queue reaches 128MB in
size, new incoming stats are dropped until a backend connection is
successful and the queue begins to drain.

All log messages are sent to syslog with the INFO priority.

Upon SIGHUP, the config file will be reloaded and all backend
connections closed. Note that any stats in the send queue at the time
of SIGHUP will be dropped.

If SIGINT or SIGTERM are caught, all connections are killed, send
queues are dropped, and memory freed. statsrelay exits with return
code 0 if all went well.

To retrieve server statistics, connect to TCP port 8125 and send the
string "status" followed by a newline '\n' character. The end of the
status output is denoted by two consecutive newlines "\n\n"

stats example:
```
nc localhost 8125
status
global bytes_recv_udp counter 0
global bytes_recv_tcp counter 41
global total_connections counter 1
global last_reload timestamp 0
global malformed_lines counter 0
backend:127.0.0.2:8127 bytes_queued counter 27
backend:127.0.0.2:8127 bytes_sent counter 27
backend:127.0.0.2:8127 relayed_lines counter 3
backend:127.0.0.2:8127 dropped_lines counter 0

```
