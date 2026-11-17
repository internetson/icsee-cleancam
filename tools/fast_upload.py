import socket, time, io, tarfile, os, sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.remote_exec import run_cmd

print("[*] Preparing tar archive of build_clean/web-x...")
tar_buf = io.BytesIO()
with tarfile.open(fileobj=tar_buf, mode="w") as tar:
    for root, dirs, files in os.walk("build_clean/web-x"):
        for f in files:
            full_path = os.path.join(root, f)
            rel_path = os.path.relpath(full_path, "build_clean/web-x").replace("\\", "/")
            tar.add(full_path, arcname=rel_path)

tar_data = tar_buf.getvalue()
print(f"[*] Tar archive size: {len(tar_data)} bytes")

print("[*] Ensuring /mnt/web is mounted as tmpfs...")
run_cmd("mount | grep -q ' /mnt/web ' || mount -t tmpfs tmpfs /mnt/web")

print("[*] Starting nc receiver on camera port 9999...")
# Run nc in background writing to /var/tmp/web.tar
run_cmd("/bin/busybox nc -l -p 9999 > /var/tmp/web.tar &")
time.sleep(0.5)

print("[*] Sending tar data to 10.10.11.155:9999...")
s = socket.socket()
s.connect(("10.10.11.155", 9999))
s.sendall(tar_data)
s.close()
time.sleep(0.5)

print("[*] Extracting tar archive into /mnt/web...")
print(run_cmd("cd /mnt/web && /var/tmp/mmcblock0/busybox tar -xf /var/tmp/web.tar && chmod -R 755 /mnt/web && rm -f /var/tmp/web.tar"))

print("[*] Current files in /mnt/web:")
print(run_cmd("ls -la /mnt/web; ls -la /mnt/web/cgi-bin"))

print("[*] Restarting HTTP server...")
run_cmd("killall -9 httpd xm_httpd 2>/dev/null; sleep 1; /var/tmp/mmcblock0/busybox httpd -p 8080 -h /mnt/web & /var/tmp/mmcblock0/busybox httpd -p 80 -h /mnt/web &")
time.sleep(0.5)
print(run_cmd("ps | grep http"))
