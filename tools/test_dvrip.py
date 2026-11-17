import socket, struct, json, time

def test_login(magic, ver=1):
    s = socket.socket()
    s.settimeout(2.0)
    s.connect(('10.10.11.155', 34567))
    
    payload = json.dumps({
        "EncryptType": "MD5",
        "LoginType": "DVRIP-Web",
        "PassWord": "",
        "UserName": "admin"
    }).encode('utf-8') + b'\n\x00'
    
    # 20-byte DVRIP header
    hdr = struct.pack('<BBBBIIHHI', magic, ver, 0, 0, 0, 1, 0, 1000, len(payload))
    s.sendall(hdr + payload)
    
    try:
        data = s.recv(1024)
        print(f"Magic 0x{magic:02x} ver={ver}: Received {len(data)} bytes:")
        if len(data) >= 20:
            h_magic, h_ver, _, _, sess, seq, _, msgid, dlen = struct.unpack('<BBBBIIHHI', data[:20])
            print(f"  Response: magic=0x{h_magic:02x}, msgid={msgid}, sess={hex(sess)}, dlen={dlen}")
            print(f"  Body: {data[20:20+dlen]}")
        return True
    except socket.timeout:
        print(f"Magic 0x{magic:02x} ver={ver}: TIMEOUT (no reply)")
        return False
    finally:
        s.close()

test_login(0xff, 1)
test_login(0xff, 0)
test_login(0x5a, 0)
test_login(0x5a, 1)
