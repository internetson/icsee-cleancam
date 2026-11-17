import sys
sys.path.insert(0, 'tools')
from remote_exec import run_cmd

print("Read back:")
print(run_cmd("""/var/tmp/xm_setcfg 1042 '{"Name":"AVEnc.VideoWidget"}'"""))
