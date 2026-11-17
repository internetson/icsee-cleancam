import socket, time, sys

sys.stdout.reconfigure(encoding='utf-8')

def run_cmd(cmd, timeout=5.0, host='10.10.11.155'):
    s = socket.socket()
    s.settimeout(2.0)
    try:
        s.connect((host, 23))
    except Exception as e:
        return f"Connection to {host}:23 failed: {e}"
    
    # Send newline to trigger telnetd
    s.sendall(b'\n')
    time.sleep(0.1)
    
    # Read IAC negotiation
    buf = b''
    try:
        buf = s.recv(1024)
    except:
        pass
        
    # Reply to IAC negotiation (WONT/DONT)
    reply = bytearray()
    i = 0
    while i < len(buf):
        if buf[i] == 0xff and i + 2 < len(buf):
            cmd_type = buf[i+1]
            opt = buf[i+2]
            if cmd_type == 0xfd: # DO -> reply WONT
                reply.extend([0xff, 0xfc, opt])
            elif cmd_type == 0xfb: # WILL -> reply DONT
                reply.extend([0xff, 0xfe, opt])
            i += 3
        else:
            i += 1
            
    if reply:
        s.sendall(bytes(reply))
        time.sleep(0.1)
        
    # Drain prompt
    s.settimeout(0.3)
    try:
        s.recv(1024)
    except:
        pass
        
    marker = "DONE_MARKER_99"
    payload = f"export PATH=/bin:/sbin:/usr/bin:/usr/sbin\n{cmd}\necho {marker}\n".encode()
    s.sendall(payload)
    
    res = b''
    start = time.time()
    s.settimeout(0.5)
    while time.time() - start < timeout:
        try:
            chunk = s.recv(4096)
            if not chunk: break
            res += chunk
            if marker.encode() in res:
                break
        except socket.timeout:
            continue
        except:
            break
            
    s.close()
    
    text = res.decode('utf-8', errors='replace')
    lines = text.splitlines()
    out = []
    capture = False
    for line in lines:
        if marker in line:
            break
        if "export PATH=" in line:
            capture = True
            continue
        if capture:
            out.append(line)
            
    if not out:
        # Fallback if export PATH line wasn't echoed
        for line in lines:
            if marker in line or line.strip() == '#' or line.strip() == cmd.strip():
                continue
            out.append(line)
            
    return '\n'.join(out).strip()

if __name__ == '__main__':
    cmd = ' '.join(sys.argv[1:]) if len(sys.argv) > 1 else 'id'
    print(run_cmd(cmd))
