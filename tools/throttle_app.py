import base64, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from remote_exec import run_cmd

script = '''#!/bin/sh
echo "=== App starting ===" >> /var/tmp/app_debug.log
/usr/bin/App >> /var/tmp/app_debug.log 2>&1
EC=$?
echo "=== App EXITED with code $EC ===" >> /var/tmp/app_debug.log
sleep 60
'''

b64 = base64.b64encode(script.encode()).decode()

cmd = f'''echo "{b64}" | /var/tmp/mmcblock0/busybox base64 -d > /var/tmp/AppRun.sh
chmod 755 /var/tmp/AppRun.sh
mount -o bind /var/tmp/AppRun.sh /usr/sbin/AppRun.sh
/var/tmp/mmcblock0/busybox killall -9 App
'''

print("Applying bind mount...")
print(run_cmd(cmd))
