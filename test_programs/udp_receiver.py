import socket

server_address = '10.2.164.5'
server_port = 31337

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s: 
    s.bind((server_address, server_port))
    print("Binding to " + server_address + ":" + str(server_port))
    while True:
        payload, client_address = s.recvfrom(1024)
        print(str(payload))
        sent = s.sendto(payload, client_address)

