import sys, os
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.remote_exec import run_cmd

print(run_cmd('cat /mnt/custom/CustomConfig/AVEnc.custom'))
