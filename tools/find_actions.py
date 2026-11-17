with open(r'c:\Users\sockm\Documents\Git Projects\icsee\extracted\user-x\bin\App', 'rb') as f:
    data = f.read()

import re
pos = data.find(b'CmdsMonitor::MonitorReq')
chunk = data[pos-1000:pos+3000]
# Find quoted strings or strings like Start, Stop, Claim, etc.
actions = [b'Start', b'Stop', b'Claim', b'Pause', b'Resume', b'Play', b'Live', b'Monitor']
for a in actions:
    print(a.decode(), a in chunk)
