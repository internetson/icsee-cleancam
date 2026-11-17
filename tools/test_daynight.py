import sys
sys.path.insert(0, 'tools')
from remote_exec import run_cmd

print("--- Set DayNightColor to 0x00000002 (Night/IR) ---")
res = run_cmd('''/var/tmp/xm_setcfg 1040 '{"Name":"Camera.Param","Camera.Param":[{"DayNightColor":"0x00000002"}]}' ''')
print(res)

print("--- Set DayNightColor back to 0x00000005 (Auto) ---")
res2 = run_cmd('''/var/tmp/xm_setcfg 1040 '{"Name":"Camera.Param","Camera.Param":[{"DayNightColor":"0x00000005"}]}' ''')
print(res2)
