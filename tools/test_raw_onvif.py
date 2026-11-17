import socket

s = socket.socket()
s.settimeout(3.0)
s.connect(('10.10.11.155', 8899))
body = '<?xml version="1.0" encoding="UTF-8"?><s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"><s:Body><GetProfiles xmlns="http://www.onvif.org/ver10/media/wsdl"/></s:Body></s:Envelope>'
http_req = f'POST /onvif/media_service HTTP/1.1\r\nHost: 10.10.11.155:8899\r\nContent-Type: application/soap+xml; charset=utf-8\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n{body}'
s.sendall(http_req.encode())
resp = b''
while True:
    try:
        chunk = s.recv(4096)
        if not chunk: break
        resp += chunk
    except: break
s.close()

print(resp.decode('utf-8', errors='ignore'))
