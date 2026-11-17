import socket, struct, json, time

def test_stream_type(stype):
    s_cmd = socket.socket()
    s_cmd.connect(('10.10.11.155', 34567))

    # Login
    login = json.dumps({'EncryptType': 'NONE', 'LoginType': 'DVRIP-Web', 'PassWord': '', 'UserName': 'admin'}).encode('utf-8') + b'\n\x00'
    hdr = struct.pack('<BBBBIIHHI', 0xFF, 1, 0, 0, 0, 1, 0, 1000, len(login))
    s_cmd.sendall(hdr + login)
    resp = s_cmd.recv(1024)
    sess = struct.unpack('<BBBBIIHHI', resp[:20])[4]

    # Data socket
    s_data = socket.socket()
    s_data.connect(('10.10.11.155', 34567))

    # Claim
    claim = json.dumps({'Name': 'OPMonitor', 'SessionID': hex(sess), 'OPMonitor': {'Action': 'Claim', 'Parameter': {'Channel': 0, 'CombinType': 'NONE', 'StreamType': stype, 'TransMode': 'TCP'}}}).encode('utf-8') + b'\n\x00'
    hdr = struct.pack('<BBBBIIHHI', 0xFF, 1, 0, 0, sess, 1, 0, 1413, len(claim))
    s_data.sendall(hdr + claim)
    r1 = s_data.recv(1024)

    # Start
    start = json.dumps({'Name': 'OPMonitor', 'SessionID': hex(sess), 'OPMonitor': {'Action': 'Start', 'Parameter': {'Channel': 0, 'CombinType': 'NONE', 'StreamType': stype, 'TransMode': 'TCP'}}}).encode('utf-8') + b'\n\x00'
    hdr = struct.pack('<BBBBIIHHI', 0xFF, 1, 0, 0, sess, 2, 0, 1410, len(start))
    s_cmd.sendall(hdr + start)
    r2 = s_cmd.recv(1024)

    print(f"StreamType '{stype}': Claim reply len={len(r1)}, Start reply len={len(r2)}")
    if len(r2) >= 20:
        dlen = struct.unpack('<BBBBIIHHI', r2[:20])[8]
        print(f"  Start body: {r2[20:20+dlen]}")

    # Listen on s_data
    s_data.settimeout(3.0)
    try:
        frame_data = s_data.recv(4096)
        print(f"  Received {len(frame_data)} bytes from data socket!")
        if len(frame_data) >= 20:
            h_magic, _, _, _, _, _, _, _, f_len = struct.unpack('<BBBBIIHHI', frame_data[:20])
            print(f"  Frame header: magic=0x{h_magic:02x}, len={f_len}")
    except socket.timeout:
        print("  TIMEOUT waiting for frames on data socket")

    s_data.close()
    s_cmd.close()

for st in ['Extra1', 'Sub', 'Extra2']:
    print(f"\n--- Testing {st} ---")
    try:
        test_stream_type(st)
    except Exception as e:
        print(f"Error: {e}")
