import socket

sock = socket.socket()
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('', 8127))
sock.listen(1)

while True:
    client, addr = sock.accept()
    print 'Connection from', addr
    while True:
        data = client.recv(1024)
        if not data:
            client.close()
            break
        print repr(data)
