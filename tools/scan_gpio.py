import sys
sys.path.insert(0, 'tools')
from remote_exec import run_cmd

script = """
for pin in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65; do
    echo $pin > /sys/class/gpio/export 2>/dev/null
    if [ -d /sys/class/gpio/gpio$pin ]; then
        d=$(cat /sys/class/gpio/gpio$pin/direction 2>/dev/null)
        v=$(cat /sys/class/gpio/gpio$pin/value 2>/dev/null)
        echo "pin $pin: dir=$d val=$v"
    fi
done
"""
print(run_cmd(script))
