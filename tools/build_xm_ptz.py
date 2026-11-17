import struct
import keystone

def make_elf(code_bytes, load_addr=0x20000):
    entry_point = load_addr + 0x80
    file_size = 0x80 + len(code_bytes)
    
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
    
    p_type = 1
    p_offset = 0
    p_vaddr = load_addr
    p_paddr = load_addr
    p_filesz = file_size
    p_memsz = file_size + 0x1000
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
    // argv[1] = direction string, argv[2] = step string (optional)
    ldr r0, [sp]
    cmp r0, #2
    blt exit_ok

    ldr r4, [sp, #8]    // r4 = direction string pointer

    // Create socket: socket(AF_INET=2, SOCK_STREAM=1, 0)
    mov r0, #2
    mov r1, #1
    mov r2, #0
    mov r7, #281        // __NR_socket
    svc #0
    cmp r0, #0
    blt exit_ok
    mov r8, r0          // r8 = sock_fd

    // Connect to 127.0.0.1:34567
    // sockaddr_in struct:
    // sin_family = AF_INET (2)
    // sin_port = htons(34567) = 0x0787 (34567 = 0x8707 -> in network order: 0x0787)
    // sin_addr = 127.0.0.1 = 0x0100007f
    sub sp, sp, #16
    mov r3, #2
    strh r3, [sp]
    ldr r3, =0x0787     // htons(34567)
    strh r3, [sp, #2]
    ldr r3, =0x0100007f // 127.0.0.1
    str r3, [sp, #4]

    mov r0, r8
    mov r1, sp
    mov r2, #16
    mov r7, #283        // __NR_connect
    svc #0
    add sp, sp, #16
    cmp r0, #0
    blt close_sock

    // Build JSON payload dynamically into stack or buffer
    // {"Name":"OPPTZControl","OPPTZControl":{"Command":"<dir>","Parameter":{"Channel":0,"Step":4}}}
    ldr r0, =pkt_buf + 20
    ldr r1, =json_p1
    bl str_copy
    mov r5, r0

    mov r0, r5
    mov r1, r4
    bl str_copy
    mov r5, r0

    mov r0, r5
    ldr r1, =json_p2
    bl str_copy
    mov r5, r0

    // Null terminate
    mov r3, #0x0a
    strb r3, [r5], #1
    mov r3, #0
    strb r3, [r5], #1

    // Calculate payload length
    ldr r2, =pkt_buf + 20
    sub r6, r5, r2      // r6 = payload_len

    // Fill 20-byte DVRIP header:
    // [0] = 0x5a
    // [1] = 0x00
    // [2..3] = 0x0000
    // [4..7] = session_id (0)
    // [8..11] = seq (1)
    // [12] = 0
    // [13] = 0
    // [14..15] = msg_id (1400 = 0x0578)
    // [16..19] = length (r6)
    ldr r0, =pkt_buf
    mov r3, #0x5a
    strb r3, [r0]
    mov r3, #0
    strb r3, [r0, #1]
    strh r3, [r0, #2]
    str r3, [r0, #4]    // session_id = 0
    mov r3, #1
    str r3, [r0, #8]    // seq = 1
    mov r3, #0
    strh r3, [r0, #12]
    ldr r3, =1400       // MSG_PTZ_CONTROL
    strh r3, [r0, #14]
    str r6, [r0, #16]   // length

    // Send packet
    mov r0, r8
    ldr r1, =pkt_buf
    add r2, r6, #20
    mov r7, #4          // write / send
    svc #0

    // If direction is not Stop, sleep 300ms and send Stop
    ldr r1, =str_stop
    mov r0, r4
    bl str_cmp
    cmp r0, #0
    beq close_sock

    // nanosleep 250ms: struct timespec { tv_sec = 0, tv_nsec = 250000000 }
    sub sp, sp, #8
    mov r3, #0
    str r3, [sp]
    ldr r3, =250000000
    str r3, [sp, #4]
    mov r0, sp
    mov r1, #0
    mov r7, #162        // __NR_nanosleep
    svc #0
    add sp, sp, #8

    // Send Stop packet
    ldr r0, =pkt_buf + 20
    ldr r1, =json_stop
    bl str_copy
    mov r5, r0
    mov r3, #0x0a
    strb r3, [r5], #1
    mov r3, #0
    strb r3, [r5], #1

    ldr r2, =pkt_buf + 20
    sub r6, r5, r2

    ldr r0, =pkt_buf
    str r6, [r0, #16]
    mov r0, r8
    ldr r1, =pkt_buf
    add r2, r6, #20
    mov r7, #4
    svc #0

close_sock:
    mov r0, r8
    mov r7, #6          // close
    svc #0

exit_ok:
    mov r0, #0
    mov r7, #1          // exit
    svc #0

// Helper: copy string from r1 to r0, returns pointer to end of copied string in r0
str_copy:
    push {r1, r2, lr}
sc_loop:
    ldrb r2, [r1], #1
    cmp r2, #0
    beq sc_done
    strb r2, [r0], #1
    b sc_loop
sc_done:
    pop {r1, r2, pc}

// Helper: compare string r0 and r1, returns 0 if match
str_cmp:
    push {r2, r3, lr}
cm_loop:
    ldrb r2, [r0], #1
    ldrb r3, [r1], #1
    cmp r2, r3
    bne cm_diff
    cmp r2, #0
    beq cm_match
    b cm_loop
cm_diff:
    mov r0, #1
    pop {r2, r3, pc}
cm_match:
    mov r0, #0
    pop {r2, r3, pc}

.balign 4
json_p1:
    .ascii "{\\"Name\\":\\"OPPTZControl\\",\\"OPPTZControl\\":{\\"Command\\":\\""
    .byte 0

json_p2:
    .ascii "\\",\\"Parameter\\":{\\"Channel\\":0,\\"Step\\":4}}}"
    .byte 0

json_stop:
    .ascii "{\\"Name\\":\\"OPPTZControl\\",\\"OPPTZControl\\":{\\"Command\\":\\"DirectionStop\\",\\"Parameter\\":{\\"Channel\\":0,\\"Step\\":0}}}"
    .byte 0

str_stop:
    .ascii "DirectionStop"
    .byte 0

.ltorg

.balign 4
pkt_buf:
    .space 512
'''

ks = keystone.Ks(keystone.KS_ARCH_ARM, keystone.KS_MODE_ARM)
enc, count = ks.asm(asm_code, 0x20080)
print(f"Assembled {count} instructions, size: {len(enc)} bytes")

elf = make_elf(bytes(enc), load_addr=0x20000)
out_file = "build_clean/custom-x/xm_ptz"
with open(out_file, "wb") as f:
    f.write(elf)
print(f"Created standalone ARM PTZ binary: {out_file} ({len(elf)} bytes)")
