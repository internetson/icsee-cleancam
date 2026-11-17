import sys
sys.path.insert(0, 'tools')
from remote_exec import run_cmd

print("=== Search strings in App for ircut ===")
print(run_cmd("strings /usr/bin/App | grep -i ircut | head -n 20"))
print("=== Search strings in App for whitelight ===")
print(run_cmd("strings /usr/bin/App | grep -i whitelight | head -n 20"))
print("=== Search strings in App for reset ===")
print(run_cmd("strings /usr/bin/App | grep -i reset | head -n 30"))
print("=== Search strings in App for gpio ===")
print(run_cmd("strings /usr/bin/App | grep -i gpio | head -n 30"))
