import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
dest = ('localhost', 8125)
#sock.connect(dest)

while True:
    sock.sendto('test:1|c\n', 0, dest)
