import socket, struct

def probe_dvrip(ip):
    try:
        s = socket.socket()
        s.settimeout(1.5)
        s.connect((ip, 34567))
        pkt = bytearray([0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        body = b'{"EncryptType":"MD5","LoginType":"DVRIP-Web","PassWord":"","UserName":"admin"}\n\x00'
        pkt[16:20] = struct.pack('<I', len(body))
        s.sendall(pkt + body)
        hdr = s.recv(20)
        sz = struct.unpack('<I', hdr[16:20])[0]
        resp = s.recv(sz)
        print(ip, 'DVRIP login response:', resp.decode('utf-8', errors='ignore'))
        s.close()
    except Exception as e:
        print(ip, 'error:', e)

for ip in ['10.10.11.152', '10.10.11.153', '10.10.11.154']:
    probe_dvrip(ip)
