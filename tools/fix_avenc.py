import socket, time, base64

with open('build_clean/custom-x/CustomConfig/AVEnc.custom', 'rb') as f:
    raw_data = f.read()

b64_data = base64.b64encode(raw_data).decode('ascii')
print(f"Uploading AVEnc.custom ({len(raw_data)} bytes, b64 {len(b64_data)} chars)...")

s = socket.socket()
s.settimeout(5)
s.connect(('10.10.11.155', 23))
time.sleep(0.3)
try: s.recv(1024)
except: pass

def send_cmd(cmd, wait=0.5):
    print(">", cmd[:60])
    s.sendall(cmd.encode() + b'\n')
    time.sleep(wait)
    res = b''
    try:
        s.settimeout(0.5)
        while True:
            chunk = s.recv(4096)
            if not chunk: break
            res += chunk
    except: pass
    return res.decode('utf-8', errors='replace')

# 1. Stop crash loop
send_cmd('killall -9 XmServices_Mgr AppRun.sh App', 1.0)

# 2. Write file
send_cmd(f'echo "{b64_data}" | /var/tmp/mmcblock0/busybox base64 -d > /var/tmp/AVEnc.custom', 0.5)

# 3. Bind mount over /mnt/custom/CustomConfig/AVEnc.custom
send_cmd('mount --bind /var/tmp/AVEnc.custom /mnt/custom/CustomConfig/AVEnc.custom', 0.5)

# 4. Remove cached AVEnc configs in /mnt/mtd/Config/Json
send_cmd('rm -f /mnt/mtd/Config/Json/AVEnc*', 0.5)

# 5. Check verification
out = send_cmd('ls -la /var/tmp/AVEnc.custom; /var/tmp/mmcblock0/busybox head -n 15 /mnt/custom/CustomConfig/AVEnc.custom', 1.0)
print("Output:\n", out)

s.close()
