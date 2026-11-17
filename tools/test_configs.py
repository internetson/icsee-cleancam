import sys
sys.path.insert(0, 'tools')
from remote_exec import run_cmd

configs = [
    'Camera.Param',
    'Camera.ParamEx',
    'AVEnc.Encode',
    'AVEnc.VideoColor',
    'AVEnc.VideoWidget',
    'fVideo.Volume',
    'Detect.MotionDetect',
    'Detect.DetectTrack',
    'Camera.WhiteLight',
    'System.TimeZone',
    'General.General'
]

for cfg in configs:
    # DVRIP payload for GetConfig
    cmd = f'/var/tmp/xm_setcfg 1042 \'{{"Name":"{cfg}"}}\''
    res = run_cmd(cmd)
    print(f"=== {cfg} ===\n{res.strip()}\n")
