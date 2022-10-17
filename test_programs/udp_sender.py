import socket
import time

def recv(s: socket.socket):
    resp = s.recv(1024)
    print(f'Received {resp.decode()}')

server_address = '10.2.164.7'
server_port = 31337


with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client_socket:
    client_socket.connect((server_address, server_port))
    print("connected on port: ", client_socket.getsockname()[1])

    for i in range(0, 10): 
        message = 'Hello World'
        client_socket.send(message.encode())
        print('sent')
        if not i == 0:
            recv(client_socket)

        time.sleep(1)

    recv(client_socket)
    


