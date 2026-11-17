import sys
sys.path.insert(0, 'tools')
from remote_exec import run_cmd

payload = '{"Name":"AVEnc.VideoWidget","AVEnc.VideoWidget":[{"ChannelTitle":{"Name":"CleanCam"},"ChannelTitleAttribute":{"EncodeBlend":true,"RelativePos":[256,512,255,24]},"TimeTitleAttribute":{"EncodeBlend":true,"RelativePos":[5800,512,255,24]}}]}'
res = run_cmd(f"/var/tmp/xm_setcfg 1040 '{payload}'")
print("Set OSD pos result:", res)
