import sys, os
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools.remote_exec import run_cmd

script = """
for p in /proc/[0-9]*/cwd; do
    c=$(/var/tmp/mmcblock0/busybox readlink $p 2>/dev/null)
    case "$c" in *web*) echo "$p -> $c" ;; esac
done
"""
print(run_cmd(script))
