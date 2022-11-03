import socket

server_address = '10.2.164.5'
server_port = 31337
connected = False
connected_port = 0

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s: 
    s.bind((server_address, server_port))
    print("Binding to " + server_address + ":" + str(server_port))
    while True:
        payload, (client_address, client_port)= s.recvfrom(1024)
        if connected_port != client_port:
            s.connect((client_address, client_port))
            connected_port = client_port

        print(str(payload) + " Echoing to: " + str(client_address) + ":" + str(client_port))
        s.send(payload)

    #s.shutdown(socket.SHUT_RDWR)

