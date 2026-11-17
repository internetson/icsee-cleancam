import os, sys, base64, time
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.remote_exec import run_cmd

WEB_DIR = "build_clean/web-x"

print("[*] Ensuring /mnt/web is mounted as tmpfs...")
run_cmd("mount | grep -q ' /mnt/web ' || mount -t tmpfs tmpfs /mnt/web")
run_cmd("mkdir -p /mnt/web/cgi-bin")

for root, dirs, files in os.walk(WEB_DIR):
    rel_dir = os.path.relpath(root, WEB_DIR)
    target_dir = "/mnt/web" if rel_dir == "." else f"/mnt/web/{rel_dir}".replace("\\", "/")
    run_cmd(f"mkdir -p {target_dir}")
    
    for f in files:
        local_path = os.path.join(root, f)
        target_path = f"{target_dir}/{f}"
        
        with open(local_path, "rb") as fp:
            data = fp.read()
        
        b64 = base64.b64encode(data).decode('ascii')
        print(f"[*] Uploading {target_path} ({len(data)} bytes)...")
        
        # Write via base64 decode in chunks of 500 chars
        run_cmd(f"echo -n > {target_path}.b64")
        for i in range(0, len(b64), 500):
            chunk = b64[i:i+500]
            run_cmd(f"echo -n '{chunk}' >> {target_path}.b64")
        
        run_cmd(f"/var/tmp/mmcblock0/busybox base64 -d {target_path}.b64 > {target_path} && rm -f {target_path}.b64")
        run_cmd(f"chmod 755 {target_path}")

print("[*] Verifying /mnt/web...")
print(run_cmd("ls -la /mnt/web; ls -la /mnt/web/cgi-bin"))

print("[*] Restarting HTTP server...")
run_cmd("killall -9 httpd xm_httpd 2>/dev/null; sleep 1; /var/tmp/mmcblock0/busybox httpd -p 8080 -h /mnt/web & /var/tmp/mmcblock0/busybox httpd -p 80 -h /mnt/web &")
time.sleep(1)
print(run_cmd("ps | grep http"))
