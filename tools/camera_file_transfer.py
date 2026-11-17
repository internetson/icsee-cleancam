import socket, time, base64, sys

def upload_file(local_path, remote_path, camera_ip='10.10.11.155'):
    with open(local_path, 'rb') as f:
        data = f.read()
    b64 = base64.b64encode(data).decode('ascii')
    print(f"Uploading {local_path} ({len(data)} bytes, b64 {len(b64)} chars) to {remote_path}...")

    s = socket.socket()
    s.settimeout(5)
    s.connect((camera_ip, 23))
    time.sleep(0.3)
    try: s.recv(1024)
    except: pass

    # Clean target temp b64 file
    b64_remote = remote_path + '.b64'
    s.sendall(f"rm -f {b64_remote} {remote_path}\n".encode())
    time.sleep(0.2)
    try: s.recv(1024)
    except: pass

    # Send in 512 byte chunks
    chunk_size = 512
    for i in range(0, len(b64), chunk_size):
        chunk = b64[i:i+chunk_size]
        cmd = f'echo -n "{chunk}" >> {b64_remote}\n'
        s.sendall(cmd.encode())
        time.sleep(0.05)

    time.sleep(0.3)
    try: s.recv(4096)
    except: pass

    # Decode on camera
    cmd = f"/var/tmp/mmcblock0/busybox base64 -d {b64_remote} > {remote_path} && rm -f {b64_remote} && ls -la {remote_path}\n"
    s.sendall(cmd.encode())
    time.sleep(0.5)

    res = b''
    try:
        s.settimeout(1.0)
        while True:
            chunk = s.recv(4096)
            if not chunk: break
            res += chunk
    except: pass
    s.close()
    out = res.decode('utf-8', errors='replace')
    print("Upload result:\n", out)
    return out

if __name__ == '__main__':
    local = sys.argv[1] if len(sys.argv) > 1 else 'build_clean/custom-x/CustomConfig/AVEnc.custom'
    remote = sys.argv[2] if len(sys.argv) > 2 else '/var/tmp/AVEnc.custom'
    upload_file(local, remote)
