import socket, concurrent.futures, sys

def scan_target(ip, port):
    s = socket.socket()
    s.settimeout(0.3)
    try:
        s.connect((ip, port))
        s.close()
        return (ip, port, True)
    except:
        return (ip, port, False)

targets = []
for ip_last in range(1, 255):
    ip = f'10.10.11.{ip_last}'
    for port in [23, 80, 554, 8080, 8899, 34567]:
        targets.append((ip, port))

print(f"Scanning {len(targets)} combinations...", flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=100) as executor:
    futures = [executor.submit(scan_target, ip, port) for ip, port in targets]
    for fut in concurrent.futures.as_completed(futures):
        ip, port, ok = fut.result()
        if ok:
            print(f"OPEN: {ip}:{port}", flush=True)
print("Scan done.", flush=True)
