import struct
import keystone

def make_elf(code_bytes, load_addr=0x40000):
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
    // 1. Create socket(AF_INET=2, SOCK_STREAM=1, 0)
    mov r0, #2
    mov r1, #1
    mov r2, #0
    mov r7, #281        // __NR_socket
    svc #0
    cmp r0, #0
    blt exit_err
    mov r4, r0          // r4 = srv_fd

    // 2. Set SO_REUSEADDR
    sub sp, sp, #8
    mov r3, #1
    str r3, [sp]
    mov r0, r4
    mov r1, #1          // SOL_SOCKET
    mov r2, #2          // SO_REUSEADDR
    mov r3, sp
    mov r5, #4
    str r5, [sp, #4]
    push {r5}
    mov r7, #294        // __NR_setsockopt
    svc #0
    add sp, sp, #12

    // 3. Bind to 0.0.0.0:23
    sub sp, sp, #16
    mov r3, #2          // AF_INET
    strh r3, [sp]
    ldr r3, =0x1700     // htons(23)
    strh r3, [sp, #2]
    mov r3, #0          // INADDR_ANY
    str r3, [sp, #4]

    mov r0, r4
    mov r1, sp
    mov r2, #16
    mov r7, #282        // __NR_bind
    svc #0
    add sp, sp, #16
    cmp r0, #0
    blt exit_err

    // 4. Listen(srv_fd, 5)
    mov r0, r4
    mov r1, #5
    mov r7, #284        // __NR_listen
    svc #0
    cmp r0, #0
    blt exit_err

accept_loop:
    // Reap zombie processes
    mov r0, #-1
    mov r1, #0
    mov r2, #1          // WNOHANG
    mov r3, #0
    mov r7, #114        // __NR_wait4
    svc #0

    // Accept connection
    mov r0, r4
    mov r1, #0
    mov r2, #0
    mov r7, #285        // __NR_accept
    svc #0
    cmp r0, #0
    blt accept_loop
    mov r8, r0          // r8 = conn_fd

    // Fork child
    mov r7, #2          // __NR_fork
    svc #0
    cmp r0, #0
    beq handle_conn

    // Parent closes conn_fd
    mov r0, r8
    mov r7, #6          // __NR_close
    svc #0
    b accept_loop

handle_conn:
    // Child: close listening socket
    mov r0, r4
    mov r7, #6
    svc #0

    // Send banner / prompt
    mov r0, r8
    ldr r1, =str_banner
    mov r2, #32
    mov r7, #4          // __NR_write
    svc #0

    // dup2(conn_fd, 0)
    mov r0, r8
    mov r1, #0
    mov r7, #63         // __NR_dup2
    svc #0

    // dup2(conn_fd, 1)
    mov r0, r8
    mov r1, #1
    mov r7, #63
    svc #0

    // dup2(conn_fd, 2)
    mov r0, r8
    mov r1, #2
    mov r7, #63
    svc #0

    // close original conn_fd
    mov r0, r8
    mov r7, #6
    svc #0

    // execve("/bin/sh", argv, envp)
    ldr r0, =str_sh
    sub sp, sp, #32
    str r0, [sp]        // argv[0] = "/bin/sh"
    mov r1, #0
    str r1, [sp, #4]    // argv[1] = NULL

    ldr r1, =str_env_path
    str r1, [sp, #8]    // envp[0] = PATH=...
    mov r2, #0
    str r2, [sp, #12]   // envp[1] = NULL

    mov r1, sp          // argv
    add r2, sp, #8      // envp
    mov r7, #11         // __NR_execve
    svc #0

    mov r0, #0
    mov r7, #1
    svc #0

exit_err:
    mov r0, #1
    mov r7, #1
    svc #0

.balign 4
str_banner:
    .asciz "CleanCam Root Shell Ready\r\n# "
.balign 4
str_sh:
    .asciz "/bin/sh"
.balign 4
str_env_path:
    .asciz "PATH=/bin:/sbin:/usr/bin:/usr/sbin"
'''

ks = keystone.Ks(keystone.KS_ARCH_ARM, keystone.KS_MODE_ARM)
enc, count = ks.asm(asm_code, 0x40080)
print(f"Assembled {count} instructions, code size: {len(enc)} bytes")

elf = make_elf(bytes(enc), load_addr=0x40000)
out_file = "build_clean/custom-x/xm_telnetd"
with open(out_file, "wb") as f:
    f.write(elf)
print(f"Created standalone ARM telnetd binary: {out_file} ({len(elf)} bytes)")
