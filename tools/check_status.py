import socket, time

s = socket.socket()
s.connect(('10.10.11.155', 23))
time.sleep(0.3)
s.recv(1024)
s.sendall(b'netstat -tln; ps | grep App\n')
time.sleep(1)
res = b''
try:
    s.settimeout(1.0)
    while True:
        chunk = s.recv(4096)
        if not chunk: break
        res += chunk
except: pass
s.close()
print(res.decode('utf-8', errors='replace'))
