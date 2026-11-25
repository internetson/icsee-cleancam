import socket, time, os, sys, gzip

RECV_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "recv_file")

def ensure_recv_file(s_telnet):
    s_telnet.sendall(b"[ -x /var/tmp/recv_file ] && echo 'RF_OK' || echo 'RF_MISSING'\n")
    time.sleep(0.3)
    out = b""
    while True:
        try:
            s_telnet.settimeout(0.3)
            c = s_telnet.recv(1024)
            if not c: break
            out += c
            if b"RF_OK" in out or b"RF_MISSING" in out: break
        except:
            break
    if b"RF_OK" in out:
        print("[+] recv_file already present on camera")
        return

    print("[*] Uploading recv_file helper...")
    with open(RECV_BIN, "rb") as f:
        recv_data = f.read()
    recv_gz = gzip.compress(recv_data, 9)

    s_telnet.sendall(b"rm -f /var/tmp/rf.gz /var/tmp/recv_file\n")
    time.sleep(0.1)

    chunk = 64
    for i in range(0, len(recv_gz), chunk):
        part = recv_gz[i:i+chunk]
        hex_escaped = "".join(f"\\x{b:02x}" for b in part)
        cmd = f'printf "{hex_escaped}" >> /var/tmp/rf.gz\n'
        s_telnet.sendall(cmd.encode("ascii"))
        time.sleep(0.01)

    cmd = b"gunzip -f /var/tmp/rf.gz && mv -f /var/tmp/rf /var/tmp/recv_file && chmod 755 /var/tmp/recv_file\n"
    s_telnet.sendall(cmd)
    time.sleep(0.8)
    print("[+] recv_file installed successfully!")

def deploy_to_camera(cam_ip):
    print(f"==================================================")
    print(f"[*] Deploying new xm_rtspd to {cam_ip}...")
    print(f"==================================================")

    rtspd_path = "build_clean/custom-x/xm_rtspd"
    with open(rtspd_path, "rb") as f:
        rtspd_data = f.read()
    print(f"[*] Local xm_rtspd binary: {len(rtspd_data)} bytes")

    # 1. Connect telnet
    s_telnet = socket.socket()
    s_telnet.settimeout(5.0)
    s_telnet.connect((cam_ip, 23))
    time.sleep(0.3)
    s_telnet.recv(1024)

    # Ensure helper exists
    ensure_recv_file(s_telnet)

    # Kill existing xm_rtspd and old recv_file
    s_telnet.sendall(b"killall -9 xm_rtspd recv_file 2>/dev/null\n")
    time.sleep(0.3)

    # Start recv_file on 9999
    s_telnet.sendall(b"/var/tmp/recv_file 9999 /var/tmp/xm_rtspd &\n")
    time.sleep(0.5)

    # 2. Connect to port 9999 and stream raw binary
    s_upload = socket.socket()
    s_upload.settimeout(5.0)
    s_upload.connect((cam_ip, 9999))
    s_upload.sendall(rtspd_data)
    s_upload.close()
    print(f"[+] Sent {len(rtspd_data)} bytes to {cam_ip}:9999 successfully!")
    time.sleep(0.5)

    # 3. Make executable, persist to /mnt/custom, and launch under watchdog loop
    cmd = (
        b"chmod 755 /var/tmp/xm_rtspd && "
        b"mount -o remount,rw /mnt/custom 2>/dev/null; "
        b"cp -f /var/tmp/xm_rtspd /mnt/custom/xm_rtspd 2>/dev/null; sync; "
        b"( while true; do /var/tmp/xm_rtspd >/var/tmp/rtspd.log 2>&1; sleep 2; done ) &\n"
    )
    s_telnet.sendall(cmd)
    time.sleep(1.0)

    out = b""
    while True:
        try:
            s_telnet.settimeout(0.5)
            c = s_telnet.recv(4096)
            if not c: break
            out += c
        except:
            break
    print(f"[+] Status:\n{out.decode('utf-8', errors='replace')}")

    time.sleep(2.0)
    s_telnet.sendall(b"ps | grep xm_rtspd; head -n 25 /var/tmp/rtspd.log\n")
    time.sleep(0.5)
    out2 = b""
    while True:
        try:
            s_telnet.settimeout(0.5)
            c = s_telnet.recv(4096)
            if not c: break
            out2 += c
        except:
            break
    print(f"[+] Log:\n{out2.decode('utf-8', errors='replace')}")
    s_telnet.close()

    # 4. Verify RTSP port 554 is accepting connections
    s_test = socket.socket()
    s_test.settimeout(2.0)
    try:
        s_test.connect((cam_ip, 554))
        print(f"[SUCCESS] Port 554 is OPEN on {cam_ip}!")
        s_test.close()
    except Exception as e:
        print(f"[-] Port 554 test failed on {cam_ip}: {e}")

if __name__ == "__main__":
    cams = sys.argv[1:] if len(sys.argv) > 1 else ["10.10.11.152", "10.10.11.153", "10.10.11.154"]
    for cam in cams:
        try:
            deploy_to_camera(cam)
        except Exception as e:
            print(f"[-] Error deploying to {cam}: {e}")
