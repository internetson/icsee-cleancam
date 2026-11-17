import struct
import keystone

def make_elf(code_bytes, load_addr=0x30000):
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
    p_memsz = file_size + 0x2000
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
    // Parse port from argv[1] if provided, default to 8080
    ldr r6, [sp]        // argc
    ldr r5, =8080       // default port = 8080
    cmp r6, #2
    blt port_default
    ldr r1, [sp, #8]    // argv[1]
    mov r2, #0          // accumulator
parse_p_loop:
    ldrb r0, [r1], #1
    cmp r0, #0
    beq parse_p_done
    cmp r0, #'0'
    blt parse_p_done
    cmp r0, #'9'
    bgt parse_p_done
    sub r0, r0, #'0'
    mov r3, #10
    mul r2, r3, r2
    add r2, r2, r0
    b parse_p_loop
parse_p_done:
    cmp r2, #0
    beq port_default
    mov r5, r2
port_default:
    // Convert r5 (host byte order) to network byte order (htons)
    and r0, r5, #0xff
    lsl r0, r0, #8
    lsr r1, r5, #8
    and r1, r1, #0xff
    orr r9, r0, r1      // r9 = port in network byte order

    // 1. Create socket: socket(AF_INET=2, SOCK_STREAM=1, 0)
    mov r0, #2
    mov r1, #1
    mov r2, #0
    mov r7, #281        // __NR_socket
    svc #0
    cmp r0, #0
    blt exit_err
    mov r4, r0          // r4 = srv_fd

    // 2. Set SO_REUSEADDR (level=1, optname=2, optval=&1, optlen=4)
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

    // 3. Bind to port (0.0.0.0:port)
    sub sp, sp, #16
    mov r3, #2          // AF_INET
    strh r3, [sp]
    strh r9, [sp, #2]   // port in network byte order
    mov r3, #0          // INADDR_ANY (0.0.0.0)
    str r3, [sp, #4]

    mov r0, r4
    mov r1, sp
    mov r2, #16
    mov r7, #282        // __NR_bind
    svc #0
    add sp, sp, #16
    cmp r0, #0
    blt exit_err

    // 4. Listen(srv_fd, 10)
    mov r0, r4
    mov r1, #10
    mov r7, #284        // __NR_listen
    svc #0
    cmp r0, #0
    blt exit_err

accept_loop:
    // Reap any finished child zombie processes
    mov r0, #-1
    mov r1, #0
    mov r2, #1          // WNOHANG
    mov r3, #0
    mov r7, #114        // __NR_wait4
    svc #0

    // Accept incoming connection
    mov r0, r4
    mov r1, #0
    mov r2, #0
    mov r7, #285        // __NR_accept
    svc #0
    cmp r0, #0
    blt accept_loop
    mov r8, r0          // r8 = conn_fd

    // Fork child worker process
    mov r7, #2          // __NR_fork
    svc #0
    cmp r0, #0
    beq handle_client   // In child: handle request

    // In parent: close conn_fd and wait for next connection
    mov r0, r8
    mov r7, #6          // __NR_close
    svc #0
    b accept_loop

handle_client:
    // In child: close listening socket r4
    mov r0, r4
    mov r7, #6
    svc #0

    // Read HTTP request header from conn_fd
    mov r0, r8
    ldr r1, =req_buf
    mov r2, #2048
    mov r7, #3          // __NR_read
    svc #0
    cmp r0, #4
    ble client_done
    mov r6, r0          // r6 = bytes read

    // Null-terminate request buffer
    ldr r1, =req_buf
    mov r2, #0
    strb r2, [r1, r6]

    // Identify method: check if starts with "GET " or "POST "
    ldrb r2, [r1]
    cmp r2, #'G'
    beq method_get
    cmp r2, #'P'
    beq method_post
    b client_done

method_get:
    ldr r10, =str_get
    add r5, r1, #4      // r5 points to URL path (after "GET ")
    b parse_url

method_post:
    ldr r10, =str_post
    add r5, r1, #5      // r5 points to URL path (after "POST ")

parse_url:
    // Skip leading spaces
skip_space:
    ldrb r2, [r5]
    cmp r2, #' '
    bne url_start
    add r5, r5, #1
    b skip_space

url_start:
    // Find end of URL (space) or start of query string ('?')
    mov r9, #0          // r9 = pointer to query string (0 if none)
    mov r1, r5
find_url_end:
    ldrb r2, [r1]
    cmp r2, #' '
    beq url_ended
    cmp r2, #'\r'
    beq url_ended
    cmp r2, #'\n'
    beq url_ended
    cmp r2, #'?'
    beq url_has_query
    cmp r2, #0
    beq url_ended
    add r1, r1, #1
    b find_url_end

url_has_query:
    mov r2, #0
    strb r2, [r1], #1   // Null-terminate path at '?'
    mov r9, r1          // r9 points to query string
    // Now continue finding space to terminate query string
find_q_end:
    ldrb r2, [r1]
    cmp r2, #' '
    beq q_ended
    cmp r2, #'\r'
    beq q_ended
    cmp r2, #'\n'
    beq q_ended
    cmp r2, #0
    beq q_ended
    add r1, r1, #1
    b find_q_end
q_ended:
    mov r2, #0
    strb r2, [r1]
    b check_cgi

url_ended:
    mov r2, #0
    strb r2, [r1]

check_cgi:
    // Check if path starts with "/cgi-bin/"
    ldr r0, =str_cgiprefix
    mov r1, r5
    bl str_startswith
    cmp r0, #0
    bne is_cgi_request

    // --- STATIC FILE REQUEST ---
    // If path is "/" or empty, serve "/index.html"
    ldrb r2, [r5]
    cmp r2, #'/'
    bne check_static
    ldrb r2, [r5, #1]
    cmp r2, #0
    bne check_static
    ldr r5, =str_default_index

check_static:
    // Build full path: "/mnt/web" + path
    ldr r0, =file_path_buf
    ldr r1, =str_webroot
    bl str_copy
    mov r1, r5
    bl str_copy
    mov r2, #0
    strb r2, [r0]

    // Open file: open(file_path_buf, O_RDONLY=0)
    ldr r0, =file_path_buf
    mov r1, #0          // O_RDONLY
    mov r2, #0
    mov r7, #5          // __NR_open
    svc #0
    cmp r0, #0
    blt send_404
    mov r5, r0          // r5 = file_fd

    // Send HTTP 200 header
    mov r0, r8
    ldr r1, =hdr_200_static
    mov r2, #78         // hdr_200_static length
    mov r7, #4          // __NR_write
    svc #0

send_file_loop:
    mov r0, r5
    ldr r1, =io_buf
    mov r2, #1024
    mov r7, #3          // __NR_read
    svc #0
    cmp r0, #0
    ble file_sent
    mov r2, r0          // bytes read
    mov r0, r8          // write to conn_fd
    ldr r1, =io_buf
    mov r7, #4          // __NR_write
    svc #0
    b send_file_loop

file_sent:
    mov r0, r5
    mov r7, #6          // __NR_close
    svc #0
    b client_done

send_404:
    mov r0, r8
    ldr r1, =hdr_404
    mov r2, #86         // hdr_404 length
    mov r7, #4
    svc #0
    b client_done

    // --- CGI SCRIPT EXECUTION ---
is_cgi_request:
    // Build full script path: "/mnt/web" + path
    ldr r0, =file_path_buf
    ldr r1, =str_webroot
    bl str_copy
    mov r1, r5
    bl str_copy
    mov r2, #0
    strb r2, [r0]

    // Build environment strings
    // env[0]: REQUEST_METHOD=...
    ldr r0, =env_method
    ldr r1, =str_env_method
    bl str_copy
    mov r1, r10
    bl str_copy
    mov r2, #0
    strb r2, [r0]

    // env[1]: QUERY_STRING=...
    ldr r0, =env_query
    ldr r1, =str_env_query
    bl str_copy
    cmp r9, #0
    beq query_empty
    mov r1, r9
    bl str_copy
query_empty:
    mov r2, #0
    strb r2, [r0]

    // Send initial HTTP 200 status line
    mov r0, r8
    ldr r1, =hdr_200_cgi
    mov r2, #17         // hdr_200_cgi length
    mov r7, #4
    svc #0

    // Duplicate socket to stdout (fd 1) and stdin (fd 0)
    mov r0, r8
    mov r1, #0
    mov r7, #63         // __NR_dup2
    svc #0

    mov r0, r8
    mov r1, #1
    mov r7, #63         // __NR_dup2
    svc #0

    mov r0, r8
    mov r7, #6          // close original conn_fd
    svc #0

    // Prepare argv and envp pointers array on stack
    // argv = [file_path_buf, NULL]
    ldr r0, =file_path_buf
    sub sp, sp, #32
    str r0, [sp]        // argv[0]
    mov r1, #0
    str r1, [sp, #4]    // argv[1] = NULL

    // envp = [env_method, env_query, env_ldlib, env_path, NULL]
    ldr r1, =env_method
    str r1, [sp, #8]
    ldr r1, =env_query
    str r1, [sp, #12]
    ldr r1, =str_env_ldlib
    str r1, [sp, #16]
    ldr r1, =str_env_path
    str r1, [sp, #20]
    mov r1, #0
    str r1, [sp, #24]

    // execve(file_path_buf, argv, envp)
    ldr r0, =file_path_buf
    mov r1, sp          // argv
    add r2, sp, #8      // envp
    mov r7, #11         // __NR_execve
    svc #0

    // If execve fails, exit child immediately
client_done:
    mov r0, r8
    mov r7, #6          // __NR_close
    svc #0
    mov r0, #0
    mov r7, #1          // __NR_exit
    svc #0

exit_err:
    mov r0, #1
    mov r7, #1
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

// Helper: check if string r1 starts with prefix r0 (returns 1 if yes, 0 if no)
str_startswith:
    push {r2, r3, lr}
sw_lp:
    ldrb r2, [r0], #1
    cmp r2, #0
    beq sw_yes
    ldrb r3, [r1], #1
    cmp r2, r3
    bne sw_no
    b sw_lp
sw_yes:
    mov r0, #1
    pop {r2, r3, pc}
sw_no:
    mov r0, #0
    pop {r2, r3, pc}

.balign 4
str_get:
    .asciz "GET"
str_post:
    .asciz "POST"
str_cgiprefix:
    .asciz "/cgi-bin/"
str_default_index:
    .asciz "/index.html"
str_webroot:
    .asciz "/mnt/web"
str_env_method:
    .asciz "REQUEST_METHOD="
str_env_query:
    .asciz "QUERY_STRING="
str_env_ldlib:
    .asciz "LD_LIBRARY_PATH=/mnt/web/lib:/usr/local/lib:/usr/lib"
str_env_path:
    .asciz "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/mnt/custom"

hdr_200_static:
    .asciz "HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"

hdr_200_cgi:
    .asciz "HTTP/1.0 200 OK\r\n"

hdr_404:
    .asciz "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n404 Not Found\r\n"

.ltorg

.balign 4
req_buf:
    .space 2048
file_path_buf:
    .space 256
env_method:
    .space 64
env_query:
    .space 512
io_buf:
    .space 1024
'''

ks = keystone.Ks(keystone.KS_ARCH_ARM, keystone.KS_MODE_ARM)
enc, count = ks.asm(asm_code, 0x30080)
print(f"Assembled {count} instructions, code size: {len(enc)} bytes")

elf = make_elf(bytes(enc), load_addr=0x30000)
out_file = "build_clean/custom-x/xm_httpd"
with open(out_file, "wb") as f:
    f.write(elf)
print(f"Created standalone ARM HTTP server binary: {out_file} ({len(elf)} bytes)")
