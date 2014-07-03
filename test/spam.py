import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 8125))

while True:
    sock.sendall('test:1|c\n')
