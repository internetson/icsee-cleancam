import sys
sys.path.insert(0, 'tools')
from fast_tar_upload import upload_tar
from remote_exec import run_cmd

upload_tar({'xm_setcfg': 'xm_setcfg'})
cmd = r'/var/tmp/xm_setcfg 1042 "{\"Name\":\"Camera.Param\"}"'
print(run_cmd('chmod +x /var/tmp/xm_setcfg; ' + cmd))
