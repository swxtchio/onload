import socket

HOST = "10.2.164.5"  
PORT = 31339

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind((HOST, PORT))
    s.listen()
    print("Listening on port: "+str(PORT))
    while True:
        conn, addr = s.accept()
        with conn:
            print(f"Connected by {addr}")
            while True:
                data = conn.recv(1024)
                if not data:
                    break
                print(f"Received {data}")
                #conn.sendall(data)
            #conn.shutdown(socket.SHUT_RDWR)
