"""A sink for statsite to send metrics to multiple sinks.
"""

import argparse
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('commands', nargs='+')
    args = parser.parse_args()

    cmds = [cmd.split() for cmd in args.commands]
    children = [subprocess.Popen(c, stdin=subprocess.PIPE) for c in cmds]

    # N.B. we want to force stdin to be read iteratively, to avoid
    # buffering when statsite is sending a large amount of
    # data. This means that we must explicitly get lines using
    # stdin.readline(), and we can't use a map or iterator over
    # stdin.
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        for child in children:
            try:
                child.stdin.write(line)
            except:
                pass  # Do something more sophisticated?


if __name__ == '__main__':
    main()
