import keystone
import capstone
import struct

asm1 = '''
.global _start
_start:
    ldr r1, =test_str
    mov r0, #0
    mov r7, #1
    svc #0
test_str:
    .ascii "Hello Arm!\n"
'''

ks = keystone.Ks(keystone.KS_ARCH_ARM, keystone.KS_MODE_ARM)
enc, count = ks.asm(asm1, 0x10080)
print('With same section:')
cs = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
for i in cs.disasm(bytes(enc), 0x10080):
    print(hex(i.address), i.mnemonic, i.op_str)
# Print all words in enc
for idx in range(0, len(enc), 4):
    w = struct.unpack('<I', bytes(enc)[idx:idx+4])[0] if idx+4 <= len(enc) else 0
    print(f'Offset 0x{idx:x} (addr 0x{0x10080+idx:x}): 0x{w:08x}')
