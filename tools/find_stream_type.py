with open(r'c:\Users\sockm\Documents\Git Projects\icsee\extracted\user-x\bin\App', 'rb') as f:
    data = f.read()

import re
# Search for 'Main' string comparisons
pos = 0
while True:
    pos = data.find(b'Main', pos)
    if pos == -1: break
    if data[pos:pos+5] in (b'Main\x00', b'MainFormat', b'MainStream'):
        # print context
        print(hex(pos), data[max(0, pos-10):pos+30])
    pos += 1
