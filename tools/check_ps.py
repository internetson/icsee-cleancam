import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from remote_exec import run_cmd
print(run_cmd("ps | grep -E 'App|Xm'"))
