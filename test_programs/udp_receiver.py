import socket

server_address = '0.0.0.0'
server_port = 31337

print("Binding to " + server_address + ":" + str(server_port))
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s: 
    s.bind((server_address, server_port))
    while True:
        payload, client_address = s.recvfrom(1024)
        print(str(payload))
        s.sendto(payload, client_address)
