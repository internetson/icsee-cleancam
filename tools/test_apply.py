import sys
sys.path.insert(0, 'tools')
from remote_exec import run_cmd

test_cfgs = [
    ('Flip/Mirror', '1040', '{"Name":"Camera.Param","Camera.Param":[{"PictureFlip":"0x00000000","PictureMirror":"0x00000000"}]}'),
    ('WhiteLight', '1040', '{"Name":"Camera.WhiteLight","Camera.WhiteLight":{"WorkMode":"Auto","Brightness":50}}'),
    ('OSD Widget', '1040', '{"Name":"AVEnc.VideoWidget","AVEnc.VideoWidget":[{"ChannelTitle":{"Name":"CleanCam"},"ChannelTitleAttribute":{"EncodeBlend":true},"TimeTitleAttribute":{"EncodeBlend":true}}]}'),
    ('Volume', '1040', '{"Name":"fVideo.Volume","fVideo.Volume":[{"LeftVolume":80,"RightVolume":80}]}'),
    ('DetectTrack', '1040', '{"Name":"Detect.DetectTrack","Detect.DetectTrack":{"Enable":false,"Sensitivity":2,"ReturnTime":30}}'),
    ('MotionDetect', '1040', '{"Name":"Detect.MotionDetect","Detect.MotionDetect":[{"Enable":false,"Level":3}]}')
]
for name, msgid, payload in test_cfgs:
    res = run_cmd(f"/var/tmp/xm_setcfg {msgid} '{payload}'")
    print(f"{name} -> {res.strip()}")
