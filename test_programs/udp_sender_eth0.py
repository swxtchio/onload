import socket

server_address = '10.2.164.5'
server_port = 31337

def create_server_sock() -> socket.socket:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    s.bind((server_address, server_port))
    return s


server_sock1 = create_server_sock()
try:
    server_sock2 = create_server_sock()
except Exception as err:
    print(err)

server_sock1.close()



'''
for i in range(2):
    active_sock = server_sock
    for i in range(5):
        payload, client = active_sock.recvfrom(1024)
        if i == 0:
            print("Connected by: ", client[0], client[1])
            active_sock.connect(client)
        print(payload.decode())
        active_sock.send(payload)
    #active_sock.close()
    #server_sock = create_server_sock()

server_sock.close()
'''
