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
    p_memsz = file_size + 0x4000
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
    // argv[1]: either direction name OR msg_id (e.g. 1042)
    // argv[2]: if msg_id provided, argv[2] is JSON payload string
    ldr r6, [sp]        // r6 = argc
    cmp r6, #2
    blt exit_ok

    ldr r4, [sp, #8]    // r4 = argv[1]

    // Check if argv[1] starts with 'D' (Direction...) -> PTZ command mode
    ldrb r0, [r4]
    cmp r0, #'D'
    beq ptz_mode

    // Otherwise: Generic DVRIP mode
    // argc must be >= 3
    cmp r6, #3
    blt exit_ok
    ldr r5, [sp, #12]   // r5 = argv[2] (JSON string)

    // Parse msg_id from argv[1]
    mov r2, #0          // msg_id accumulator
parse_id_loop:
    ldrb r0, [r4], #1
    cmp r0, #0
    beq parse_id_done
    cmp r0, #'0'
    blt parse_id_done
    cmp r0, #'9'
    bgt parse_id_done
    sub r0, r0, #'0'
    mov r3, #10
    mul r2, r3, r2
    add r2, r2, r0
    b parse_id_loop
parse_id_done:
    mov r9, r2          // r9 = msg_id

    // Copy JSON payload into pkt_buf + 20
    ldr r0, =pkt_buf + 20
    mov r1, r5
    bl str_copy
    // Append newline and null
    mov r3, #0x0a
    strb r3, [r0], #1
    mov r3, #0
    strb r3, [r0]

    // Calculate length
    ldr r2, =pkt_buf + 20
    sub r6, r0, r2      // r6 = payload length
    mov r10, #0         // flag: no PTZ auto-stop
    b send_packet

ptz_mode:
    // PTZ Mode: msg_id = 1400 (0x0578)
    mov r9, #1400
    mov r10, #1         // flag: do PTZ auto-stop

    // Build PTZ JSON
    ldr r0, =pkt_buf + 20
    ldr r1, =json_p1
    bl str_copy
    mov r1, r4
    bl str_copy
    mov r1, =json_p2
    bl str_copy
    mov r3, #0x0a
    strb r3, [r0], #1
    mov r3, #0
    strb r3, [r0]

    ldr r2, =pkt_buf + 20
    sub r6, r0, r2      // r6 = payload length

send_packet:
    // 1. Create socket: socket(AF_INET=2, SOCK_STREAM=1, 0)
    mov r0, #2
    mov r1, #1
    mov r2, #0
    mov r7, #281        // __NR_socket
    svc #0
    cmp r0, #0
    blt exit_ok
    mov r8, r0          // r8 = sock_fd

    // 2. Connect to 127.0.0.1:34567
    sub sp, sp, #16
    mov r3, #2          // AF_INET
    strh r3, [sp]
    ldr r3, =0x0787     // htons(34567) = 0x0787
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

    // 3. Fill 20-byte DVRIP header in pkt_buf
    ldr r0, =pkt_buf
    mov r3, #0xff
    strb r3, [r0]
    mov r3, #1
    strb r3, [r0, #1]
    mov r3, #0
    strh r3, [r0, #2]
    str r3, [r0, #4]    // session_id = 0
    mov r3, #1
    str r3, [r0, #8]    // seq = 1
    mov r3, #0
    strh r3, [r0, #12]
    strh r9, [r0, #14]  // msg_id
    str r6, [r0, #16]   // length

    // 4. Send packet: write(sock_fd, pkt_buf, 20 + payload_len)
    mov r0, r8
    ldr r1, =pkt_buf
    add r2, r6, #20
    mov r7, #4          // __NR_write
    svc #0

    // If PTZ mode and direction is not DirectionStop, wait 250ms and send Stop
    cmp r10, #1
    bne close_sock
    ldr r1, =str_stop
    mov r0, r4
    bl str_cmp
    cmp r0, #0
    beq close_sock

    // nanosleep 250ms
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

    // Send Stop command
    ldr r0, =pkt_buf + 20
    ldr r1, =json_p1
    bl str_copy
    ldr r1, =str_stop
    bl str_copy
    ldr r1, =json_p2
    bl str_copy
    mov r3, #0x0a
    strb r3, [r0], #1
    mov r3, #0
    strb r3, [r0]

    ldr r2, =pkt_buf + 20
    sub r6, r0, r2

    ldr r0, =pkt_buf
    str r6, [r0, #16]

    mov r0, r8
    ldr r1, =pkt_buf
    add r2, r6, #20
    mov r7, #4
    svc #0

close_sock:
    mov r0, r8
    mov r7, #6          // __NR_close
    svc #0

exit_ok:
    mov r0, #0
    mov r7, #1          // __NR_exit
    svc #0

// Helper: copy string from r1 to r0, returns end ptr in r0
str_copy:
    push {r1, r2, lr}
sc_lp:
    ldrb r2, [r1], #1
    cmp r2, #0
    beq sc_dn
    strb r2, [r0], #1
    b sc_lp
sc_dn:
    pop {r1, r2, pc}

// Helper: compare string r0 and r1, returns 0 if equal
str_cmp:
    push {r2, r3, lr}
cmp_lp:
    ldrb r2, [r0], #1
    ldrb r3, [r1], #1
    cmp r2, r3
    bne cmp_diff
    cmp r2, #0
    beq cmp_same
    b cmp_lp
cmp_diff:
    mov r0, #1
    pop {r2, r3, pc}
cmp_same:
    mov r0, #0
    pop {r2, r3, pc}

.balign 4
str_stop:
    .asciz "DirectionStop"
json_p1:
    .asciz "{\\"Name\\":\\"OPPTZControl\\",\\"OPPTZControl\\":{\\"Command\\":\\""
json_p2:
    .asciz "\\",\\"Parameter\\":{\\"Channel\\":0,\\"Step\\":4}}}"

.ltorg

.balign 4
pkt_buf:
    .space 4096
'''

ks = keystone.Ks(keystone.KS_ARCH_ARM, keystone.KS_MODE_ARM)
enc, count = ks.asm(asm_code, 0x20080)
print(f"Assembled {count} instructions, code size: {len(enc)} bytes")

elf = make_elf(bytes(enc), load_addr=0x20000)
for out_path in ["build_clean/custom-x/xm_cmd", "build_clean/custom-x/xm_ptz"]:
    with open(out_path, "wb") as f:
        f.write(elf)
    print(f"Created standalone ARM DVRIP binary: {out_path} ({len(elf)} bytes)")
