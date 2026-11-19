# -*- coding: utf-8 -*-
import socket, time, os, sys, gzip

RECV_BIN = r"C:\Users\sockm\.gemini\antigravity\brain\86a4e033-acb4-4ed9-a0a3-9cb07aeed087\scratch\recv_file"

def upload_file(s_telnet, cam_ip, local_path, remote_path, port=9999):
    with open(local_path, "rb") as f:
        data = f.read()

    # Kill any stale recv_file and remove target to prevent Text File Busy (ETXTBSY)
    s_telnet.sendall(f"killall -9 recv_file xm_ha_osd 2>/dev/null; rm -f {remote_path}\n".encode("ascii"))
    time.sleep(0.2)

    cmd = f"/var/tmp/recv_file {port} {remote_path} &\n".encode("ascii")
    s_telnet.sendall(cmd)
    time.sleep(0.3)

    s_up = socket.socket()
    s_up.settimeout(5.0)
    s_up.connect((cam_ip, port))
    s_up.sendall(data)
    s_up.close()
    time.sleep(0.3)
    print(f"[+] Uploaded {local_path} ({len(data)} bytes) -> {remote_path}")

def main():
    cam_ip = sys.argv[1] if len(sys.argv) > 1 else "10.10.11.152"
    print(f"[*] Connecting to {cam_ip}:23...")

    s = socket.socket()
    s.settimeout(5.0)
    s.connect((cam_ip, 23))
    time.sleep(0.3)
    s.recv(1024)

    # 1. Upload xm_ha_osd binary
    upload_file(s, cam_ip, "build_clean/custom-x/xm_ha_osd", "/var/tmp/xm_ha_osd", 9991)
    s.sendall(b"chmod 755 /var/tmp/xm_ha_osd\n")
    time.sleep(0.2)

    # 2. Upload cgi-bin scripts & index.html
    upload_file(s, cam_ip, "build_clean/web-x/cgi-bin/ha_osd", "/var/tmp/ha_osd", 9992)
    s.sendall(b"chmod 755 /var/tmp/ha_osd\n")
    time.sleep(0.2)

    upload_file(s, cam_ip, "build_clean/web-x/cgi-bin/osd_push", "/var/tmp/osd_push", 9993)
    s.sendall(b"chmod 755 /var/tmp/osd_push\n")
    time.sleep(0.2)

    upload_file(s, cam_ip, "build_clean/web-x/index.html", "/var/tmp/index.html", 9994)
    time.sleep(0.2)

    # 3. Setup ramfs web mirror and bind mount over /mnt/web
    setup_cmd = (
        b"mkdir -p /var/web /var/web/cgi-bin\n"
        b"cp -a /mnt/web/* /var/web/ 2>/dev/null\n"
        b"cp -f /var/tmp/index.html /var/web/index.html\n"
        b"cp -f /var/tmp/ha_osd /var/web/cgi-bin/ha_osd\n"
        b"cp -f /var/tmp/osd_push /var/web/cgi-bin/osd_push\n"
        b"chmod 755 /var/web/cgi-bin/*\n"
        b"umount /mnt/web 2>/dev/null; mount -o bind /var/web /mnt/web\n"
        b"killall -9 xm_ha_osd 2>/dev/null\n"
        b"( while true; do /var/tmp/xm_ha_osd >/var/tmp/ha_osd.log 2>&1; sleep 3; done ) &\n"
        b"echo 'DEPLOY_COMPLETE'\n"
    )
    s.sendall(setup_cmd)
    time.sleep(1.0)

    out = b""
    while True:
        try:
            s.settimeout(0.5)
            c = s.recv(4096)
            if not c: break
            out += c
            if b"DEPLOY_COMPLETE" in out: break
        except:
            break
    print("[+] Camera output:\n", out.decode("utf-8", errors="replace"))
    s.close()

if __name__ == "__main__":
    main()
