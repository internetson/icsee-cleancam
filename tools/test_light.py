import sys
sys.path.insert(0, 'tools')
from remote_exec import run_cmd

print("--- Turn White Light ON ---")
res1 = run_cmd("""/var/tmp/xm_setcfg 1040 '{"Name":"Camera.WhiteLight","Camera.WhiteLight":{"WorkMode":"Open","Brightness":100}}'""")
print("ON result:", res1)

print("--- Read White Light config ---")
res2 = run_cmd("""/var/tmp/xm_setcfg 1042 '{"Name":"Camera.WhiteLight"}'""")
print("Read result:", res2)
