with open(r'c:\Users\sockm\Documents\Git Projects\icsee\extracted\user-x\bin\App', 'rb') as f:
    data = f.read()

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
# Let's disassemble from 0x17dc00 to 0x17e000
code = data[0x17dc00 - 0x8000 : 0x17e000 - 0x8000]
for ins in md.disasm(code, 0x17dc00):
    if ins.mnemonic in ('bl', 'cmp', 'beq', 'bne', 'mov'):
        print(f'0x{ins.address:x}:\t{ins.mnemonic}\t{ins.op_str}')
