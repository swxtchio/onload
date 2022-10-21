import socket
import time

import socket

HOST = "10.2.164.7"  
PORT = 31339

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((HOST, PORT))
    print("connected")
    for i in range(0, 10): 
        s.sendall(b"Hello, world")
        print(f"Sent")
        #data = s.recv(1024)
        time.sleep(1)

    #s.shutdown(socket.SHUT_RDWR)

