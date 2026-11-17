with open(r'c:\Users\sockm\Documents\Git Projects\icsee\extracted\user-x\bin\App', 'rb') as f:
    data = f.read()

import re
pos = 0
while True:
    pos = data.find(b'102', pos)
    if pos == -1: break
    chunk = data[max(0, pos-40):pos+60]
    if b'Ret' in chunk:
        print(hex(pos), chunk)
    pos += 1
