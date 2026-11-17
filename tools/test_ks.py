import keystone
import capstone
import struct

asm = '''
.global _start
_start:
    ldr r1, =msg_test
    ldr r2, =msg_test_len
    mov r0, #0
    mov r7, #1
    svc #0

.balign 4
msg_test:
    .ascii "test string\\n"
msg_test_len = . - msg_test

.ltorg
'''

ks = keystone.Ks(keystone.KS_ARCH_ARM, keystone.KS_MODE_ARM)
enc, count = ks.asm(asm, 0x10080)
print('Success! Assembled:', count, len(enc))

for idx in range(0, len(enc), 4):
    w = struct.unpack('<I', bytes(enc)[idx:idx+4])[0] if idx+4 <= len(enc) else 0
    print(f'Offset 0x{idx:x} (addr 0x{0x10080+idx:x}): 0x{w:08x}')
