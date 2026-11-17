import sys
sys.path.insert(0, 'tools')
from remote_exec import run_cmd

print(run_cmd("find /proc/device-tree -iname '*key*' -o -iname '*button*' -o -iname '*reset*' -o -iname '*led*' -o -iname '*gpio*'"))
