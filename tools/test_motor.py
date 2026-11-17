from remote_exec import run_cmd

cmd = """
echo "setstep 1900 20 1000 20" > /proc/XmMotor
sleep 1
CURR_X=$(cat /proc/XmMotor | awk '/-------------- motor x/{f=1; next} /-------------- motor y/{f=0} f && /curr step/{print $4}')
CURR_Y=$(cat /proc/XmMotor | awk '/-------------- motor y/{f=1} f && /curr step/{print $4}')
echo "After move: X=$CURR_X Y=$CURR_Y"
"""
print(run_cmd(cmd))

