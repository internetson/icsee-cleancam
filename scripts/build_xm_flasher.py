import struct
import os
import keystone
import capstone

def make_elf(code_bytes, load_addr=0x10000):
    entry_point = load_addr + 0x80
    file_size = 0x80 + len(code_bytes)
    
    # 32-bit ARM ELF header (EABI5, softfp, ET_EXEC)
    e_ident = b'\x7fELF\x01\x01\x01\x00' + b'\x00' * 8
    e_type = 2          # ET_EXEC
    e_machine = 40      # EM_ARM
    e_version = 1
    e_entry = entry_point
    e_phoff = 52
    e_shoff = 0
    e_flags = 0x5000200 # EABI5
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
    
    # Program header: PT_LOAD (RWX)
    p_type = 1
    p_offset = 0
    p_vaddr = load_addr
    p_paddr = load_addr
    p_filesz = file_size
    p_memsz = file_size + 0x4000 # Stack and BSS space
    p_flags = 7         # RWX
    p_align = 0x1000
    
    prog_hdr = struct.pack(
        '<IIIIIIII',
        p_type, p_offset, p_vaddr, p_paddr,
        p_filesz, p_memsz, p_flags, p_align
    )
    
    pad = b'\x00' * (0x80 - len(elf_hdr) - len(prog_hdr))
    return elf_hdr + prog_hdr + pad + code_bytes

asm_code = '''
.global _start
_start:
    // Stack layout:
    // [sp] = argc
    // [sp, #4] = argv[0]
    // [sp, #8] = argv[1] (mtd_dev)
    // [sp, #12] = argv[2] (img_file)
    // [sp, #16] = argv[3] (part_size)
    ldr r0, [sp]
    cmp r0, #4
    blt usage

    ldr r4, [sp, #8]    // r4 = mtd_dev
    ldr r5, [sp, #12]   // r5 = img_file
    ldr r6, [sp, #16]   // r6 = part_size string

    // Parse partition size string into integer in r6
    mov r0, r6
    bl parse_int
    mov r6, r0          // r6 = part_size (int)

    // Print starting banner
    ldr r1, =msg_start
    ldr r2, =msg_start_len
    bl print_str

    mov r1, r4
    bl print_sz

    ldr r1, =msg_newline
    mov r2, #1
    bl print_str

    // Open MTD device (O_RDWR = 2)
    mov r0, r4
    mov r1, #2
    mov r7, #5          // open
    svc #0
    cmp r0, #0
    blt err_open_mtd
    mov r8, r0          // r8 = mtd_fd

    // Step 1: Xiongmai Kernel MTD Authentication & Unlock Handshake
    ldr r1, =msg_auth
    ldr r2, =msg_auth_len
    bl print_str

    sub sp, sp, #64

    // 1a. Flash_GetLockVersion (0x40044da0)
    mov r0, r8
    ldr r1, =0x40044da0
    mov r2, sp
    mov r7, #54         // ioctl
    svc #0

    // 1b. Flash_SetPassword (0x40044da1) - registers current->pid in kernel ProcCtxt
    ldr r3, =0x5a6b7c8d
    str r3, [sp]
    mov r0, r8
    ldr r1, =0x40044da1
    mov r2, sp
    mov r7, #54
    svc #0

    // 1c. Flash_VerifyPassword (0x40044da2) - matches password to grant full MTD access
    ldr r3, =0x5a6b7c8d
    str r3, [sp]
    mov r0, r8
    ldr r1, =0x40044da2
    mov r2, sp
    mov r7, #54
    svc #0

    // 1d. XmMtd_Comm_unlockUser (0x40084da8) - sets unlock_start=0, unlock_len=8MB in ProcCtxt!
    mov r3, #0
    str r3, [sp]
    str r3, [sp, #4]
    mov r0, r8
    ldr r1, =0x40084da8
    mov r2, sp
    mov r7, #54
    svc #0

    // 1e. Flash_UnlockAll (0x40044dbd) - unlock all partitions in ProcCtxt
    mov r3, #1
    str r3, [sp]
    mov r0, r8
    ldr r1, =0x40044dbd
    mov r2, sp
    mov r7, #54
    svc #0

    // 1f. Flash_UnlockRange (0x40104da6) - unlock 0 to 8MB in pLockMgr
    mov r3, #0
    str r3, [sp]
    str r3, [sp, #4]
    ldr r3, =0x00800000
    str r3, [sp, #8]
    mov r3, #0
    str r3, [sp, #12]
    mov r0, r8
    ldr r1, =0x40104da6
    mov r2, sp
    mov r7, #54
    svc #0

    // 1g. Hardware SR Write (0x40084de4) - clear SPI flash Status Register BP bits
    mov r3, #0
    str r3, [sp]
    str r3, [sp, #4]
    mov r0, r8
    ldr r1, =0x40084de4
    mov r2, sp
    mov r7, #54
    svc #0

    add sp, sp, #64

    ldr r1, =msg_auth_ok
    ldr r2, =msg_auth_ok_len
    bl print_str

    // Step 2: Erase partition block by block (64KB = 0x10000)
    ldr r1, =msg_erasing
    ldr r2, =msg_erasing_len
    bl print_str

    mov r9, #0          // current erase offset
erase_loop:
    cmp r9, r6
    bge erase_done

    sub sp, sp, #32
    str r9, [sp]
    mov r3, #0
    str r3, [sp, #4]
    ldr r3, =0x10000
    str r3, [sp, #8]
    mov r3, #0
    str r3, [sp, #12]

    mov r0, r8
    ldr r1, =0x40104d14 // MEMERASE64
    mov r2, sp
    mov r7, #54
    svc #0
    cmp r0, #0
    beq erase_next

    // Fallback: MEMERASE (32-bit struct: start, length)
    str r9, [sp]
    ldr r3, =0x10000
    str r3, [sp, #4]
    mov r0, r8
    ldr r1, =0x40084d02 // MEMERASE
    mov r2, sp
    mov r7, #54
    svc #0
    cmp r0, #0
    bne err_erase

erase_next:
    add sp, sp, #32
    ldr r3, =0x10000
    add r9, r9, r3
    b erase_loop

erase_done:
    ldr r1, =msg_erase_ok
    ldr r2, =msg_erase_ok_len
    bl print_str

    // Step 3: Open source image payload
    mov r0, r5
    mov r1, #0          // O_RDONLY
    mov r7, #5
    svc #0
    cmp r0, #0
    blt err_open_img
    mov sl, r0          // sl = img_fd

    // Rewind MTD to offset 0
    mov r0, r8
    mov r1, #0
    mov r2, #0          // SEEK_SET
    mov r7, #19         // lseek
    svc #0

    ldr r1, =msg_writing
    ldr r2, =msg_writing_len
    bl print_str

    // Step 4: Write payload loop
copy_loop:
    mov r0, sl
    ldr r1, =io_buf
    ldr r2, =4096
    mov r7, #3          // read
    svc #0
    cmp r0, #0
    ble copy_done

    mov r2, r0          // bytes to write
    mov r0, r8          // mtd_fd
    ldr r1, =io_buf
    mov r7, #4          // write
    svc #0
    cmp r0, #0
    blt err_write
    b copy_loop

copy_done:
    // Close source image
    mov r0, sl
    mov r7, #6
    svc #0

    // Fsync MTD device
    mov r0, r8
    mov r7, #118
    svc #0

    // Step 5: Bit-for-bit Verification
    ldr r1, =msg_verifying
    ldr r2, =msg_verifying_len
    bl print_str

    // Reopen source image
    mov r0, r5
    mov r1, #0
    mov r7, #5
    svc #0
    mov sl, r0

    // Rewind MTD device
    mov r0, r8
    mov r1, #0
    mov r2, #0
    mov r7, #19
    svc #0

verify_loop:
    mov r0, sl
    ldr r1, =io_buf
    ldr r2, =4096
    mov r7, #3          // read image
    svc #0
    cmp r0, #0
    ble verify_done
    mov fp, r0          // bytes to compare

    mov r0, r8
    ldr r1, =cmp_buf
    mov r2, fp
    mov r7, #3          // read flash
    svc #0
    cmp r0, fp
    bne err_verify

    // Compare buffers
    ldr r0, =io_buf
    ldr r1, =cmp_buf
    mov r2, fp
    bl mem_cmp
    cmp r0, #0
    bne err_verify

    b verify_loop

verify_done:
    // Close image
    mov r0, sl
    mov r7, #6
    svc #0

    // Close MTD
    mov r0, r8
    mov r7, #6
    svc #0

    ldr r1, =msg_success
    ldr r2, =msg_success_len
    bl print_str

    mov r0, #0
    mov r7, #1
    svc #0

usage:
    ldr r1, =msg_usage
    ldr r2, =msg_usage_len
    bl print_str
    mov r0, #1
    mov r7, #1
    svc #0

err_open_mtd:
    ldr r1, =msg_err_mtd
    ldr r2, =msg_err_mtd_len
    bl print_str
    mov r0, #2
    mov r7, #1
    svc #0

err_erase:
    ldr r1, =msg_err_erase
    ldr r2, =msg_err_erase_len
    bl print_str
    mov r0, #3
    mov r7, #1
    svc #0

err_open_img:
    ldr r1, =msg_err_img
    ldr r2, =msg_err_img_len
    bl print_str
    mov r0, #4
    mov r7, #1
    svc #0

err_write:
    ldr r1, =msg_err_write
    ldr r2, =msg_err_write_len
    bl print_str
    mov r0, #5
    mov r7, #1
    svc #0

err_verify:
    ldr r1, =msg_err_verify
    ldr r2, =msg_err_verify_len
    bl print_str
    mov r0, #6
    mov r7, #1
    svc #0

print_str:
    push {r0, r7, lr}
    mov r0, #1
    mov r7, #4
    svc #0
    pop {r0, r7, pc}

print_sz:
    push {r0, r2, r3, r7, lr}
    mov r2, #0
sz_loop:
    ldrb r3, [r1, r2]
    cmp r3, #0
    beq sz_done
    add r2, r2, #1
    b sz_loop
sz_done:
    mov r0, #1
    mov r7, #4
    svc #0
    pop {r0, r2, r3, r7, pc}

parse_int:
    push {r1, r2, r3, lr}
    mov r1, #0
pi_loop:
    ldrb r2, [r0], #1
    cmp r2, #0
    beq pi_done
    cmp r2, #'0'
    blo pi_done
    cmp r2, #'9'
    bhi pi_done
    sub r2, r2, #'0'
    mov r3, #10
    mul r1, r3, r1
    add r1, r1, r2
    b pi_loop
pi_done:
    mov r0, r1
    pop {r1, r2, r3, pc}

mem_cmp:
    push {r3, r4, lr}
mc_loop:
    cmp r2, #0
    beq mc_match
    ldrb r3, [r0], #1
    ldrb r4, [r1], #1
    cmp r3, r4
    bne mc_diff
    sub r2, r2, #1
    b mc_loop
mc_diff:
    mov r0, #1
    pop {r3, r4, pc}
mc_match:
    mov r0, #0
    pop {r3, r4, pc}

.balign 4
msg_start:
    .ascii "[*] xm_flasher: target="
msg_start_len = . - msg_start

msg_newline:
    .ascii "\\n"

msg_auth:
    .ascii "[*] Authenticating with Xiongmai MTD kernel driver...\\n"
msg_auth_len = . - msg_auth

msg_auth_ok:
    .ascii "[+] MTD kernel authentication and unlock succeeded!\\n"
msg_auth_ok_len = . - msg_auth_ok

msg_erasing:
    .ascii "[*] Erasing partition sectors...\\n"
msg_erasing_len = . - msg_erasing

msg_erase_ok:
    .ascii "[+] Erase completed successfully!\\n"
msg_erase_ok_len = . - msg_erase_ok

msg_writing:
    .ascii "[*] Writing image payload to SPI Flash...\\n"
msg_writing_len = . - msg_writing

msg_verifying:
    .ascii "[*] Verifying written flash data against source image...\\n"
msg_verifying_len = . - msg_verifying

msg_success:
    .ascii "[+] FLASH AND VERIFICATION 100% SUCCESSFUL!\\n"
msg_success_len = . - msg_success

msg_usage:
    .ascii "Usage: xm_flasher <mtd_dev> <img_file> <part_size>\\n"
msg_usage_len = . - msg_usage

msg_err_mtd:
    .ascii "[!] Error opening MTD device!\\n"
msg_err_mtd_len = . - msg_err_mtd

msg_err_erase:
    .ascii "[!] Error: Flash erase ioctl failed!\\n"
msg_err_erase_len = . - msg_err_erase

msg_err_img:
    .ascii "[!] Error opening image file!\\n"
msg_err_img_len = . - msg_err_img

msg_err_write:
    .ascii "[!] Error writing data to MTD!\\n"
msg_err_write_len = . - msg_err_write

msg_err_verify:
    .ascii "[!] Error: Flash verification mismatch!\\n"
msg_err_verify_len = . - msg_err_verify

.ltorg

.balign 4
io_buf:
    .space 4096
cmp_buf:
    .space 4096
'''

ks = keystone.Ks(keystone.KS_ARCH_ARM, keystone.KS_MODE_ARM)
encoding, count = ks.asm(asm_code, 0x10080)
print(f"Assembled {count} instructions, code size: {len(encoding)} bytes")

elf_binary = make_elf(bytes(encoding))
print(f"Total ELF binary size: {len(elf_binary)} bytes")

out_path = "sd_card_installer/xm_flasher"
with open(out_path, "wb") as f:
    f.write(elf_binary)

print(f"Written standalone ARM flasher to {out_path}!")

# Disassemble and verify the entry instructions and literal pool!
cs = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
print("\n--- Disassembly of first 15 instructions ---")
for i in cs.disasm(elf_binary[0x80:0x80+60], 0x10080):
    print(f"0x{i.address:x}: {i.mnemonic} {i.op_str}")

# Check address of msg_start in the binary
idx = bytes(encoding).find(b'[*] xm_flasher')
print(f"\nmsg_start offset in code: 0x{idx:x}, virtual address: 0x{0x10080 + idx:x}")

idx_io = bytes(encoding).find(b'\x00' * 4096)
print(f"io_buf offset in code: 0x{idx_io:x}, virtual address: 0x{0x10080 + idx_io:x}")
