import struct
import os

# We construct an ARM static executable for Linux (ARMv7-A / EABI5)
# Base virtual address: 0x10000
# ELF Header (52 bytes) + 1 Program Header (32 bytes) = 84 bytes.
# Pad headers to 0x80 (128 bytes). Code starts at 0x10080.

def make_elf(code, data):
    load_addr = 0x10000
    entry_point = load_addr + 0x80
    
    body = code + data
    file_size = 0x80 + len(body)
    
    # 32-bit ARM ELF header
    e_ident = b'\x7fELF\x01\x01\x01\x00' + b'\x00' * 8
    e_type = 2          # ET_EXEC
    e_machine = 40      # EM_ARM
    e_version = 1
    e_entry = entry_point
    e_phoff = 52        # right after ELF header
    e_shoff = 0
    e_flags = 0x5000200 # EABI5, softfp
    e_ehsize = 52
    e_phentsize = 32
    e_phnum = 1
    e_shentsize = 0
    e_shnum = 0
    e_shstrndx = 0
    
    elf_hdr = struct.pack(
        '<16sHHIIIIIHHHHHH',
        e_ident, e_type, e_machine, e_version,
        e_entry, e_phoff, e_shoff, e_flags,
        e_ehsize, e_phentsize, e_phnum,
        e_shentsize, e_shnum, e_shstrndx
    )
    
    # Program header: PT_LOAD
    p_type = 1          # PT_LOAD
    p_offset = 0
    p_vaddr = load_addr
    p_paddr = load_addr
    p_filesz = file_size
    p_memsz = file_size + 0x1000 # plenty of stack/bss
    p_flags = 7         # RWX
    p_align = 0x1000
    
    prog_hdr = struct.pack(
        '<IIIIIIII',
        p_type, p_offset, p_vaddr, p_paddr,
        p_filesz, p_memsz, p_flags, p_align
    )
    
    pad = b'\x00' * (0x80 - len(elf_hdr) - len(prog_hdr))
    return elf_hdr + prog_hdr + pad + body

# Now let's design the ARM code:
# Syscalls:
# __NR_exit = 1
# __NR_write = 4
# __NR_open = 5
# __NR_close = 6
# __NR_ioctl = 54

# In ARM:
# LDR Rd, [PC, #offset] -> e59fdXXX
def arm_ldr_pc(rd, offset):
    assert 0 <= offset < 4096
    return struct.pack('<I', 0xe59f0000 | (rd << 12) | offset)

def arm_mov_imm(rd, imm):
    assert 0 <= imm <= 255
    return struct.pack('<I', 0xe3a00000 | (rd << 12) | imm)

def arm_mov_reg(rd, rm):
    return struct.pack('<I', 0xe1a00000 | (rd << 12) | rm)

def arm_svc():
    return struct.pack('<I', 0xef000000)

def arm_cmp_imm(rn, imm):
    assert 0 <= imm <= 255
    return struct.pack('<I', 0xe3500000 | (rn << 16) | imm)

def arm_b_cond(cond, offset_words):
    # offset_words is 24-bit signed
    return struct.pack('<I', (cond << 28) | 0x0a000000 | (offset_words & 0x00ffffff))

# Build complete binary
print("ELF generator template ready")
