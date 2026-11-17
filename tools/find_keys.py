with open(r'c:\Users\sockm\Documents\Git Projects\icsee\extracted\user-x\bin\App', 'rb') as f:
    data = f.read()

import re
for m in re.finditer(rb'rtsp://[^\x00]+', data):
    print(m.group().decode())

for m in re.finditer(rb'[A-Za-z0-9_]*rtsp[A-Za-z0-9_]*', data, re.IGNORECASE):
    s = m.group().decode()
    if any(k in s.lower() for k in ['server', 'start', 'init', 'open', 'listen', 'create']):
        print(s)





