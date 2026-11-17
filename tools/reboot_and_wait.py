import sys, time, socket
sys.path.insert(0, 'tools')
from remote_exec import run_cmd

print("[*] Rebooting camera from flash...")
try:
    run_cmd('sync; reboot')
except Exception:
    pass

print("[*] Waiting 15 seconds for camera to reboot...")
time.sleep(15)

# Wait for telnet port 23
for i in range(25):
    try:
        s = socket.create_connection(('10.10.11.155', 23), timeout=2)
        s.close()
        print(f"[+] Camera is UP! (took ~{15 + i*2} seconds)")
        break
    except Exception:
        time.sleep(2)
else:
    print("[-] Camera did not respond on port 23 within 65s")
