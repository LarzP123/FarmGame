global get_time
global mem_alloc
global mem_free
global print_text
global read_text
global print_char
global print_int

; ─────────────────────────────────────────────────────────────────
; LINUX SYSCALL NUMBERS (x86-64)
; ─────────────────────────────────────────────────────────────────
SYS_READ          equ 0
SYS_WRITE         equ 1
SYS_MMAP          equ 9
SYS_MUNMAP        equ 11
SYS_CLOCK_GETTIME equ 228

STDOUT_FD         equ 1
STDIN_FD          equ 0

PROT_READ         equ 1
PROT_WRITE        equ 2
MAP_PRIVATE       equ 2
MAP_ANONYMOUS     equ 0x20

CLOCK_REALTIME    equ 0

; ─────────────────────────────────────────────────────────────────
; UNINITIALISED DATA
; ─────────────────────────────────────────────────────────────────
section .bss
    char_buf resb 1
    int_buf  resb 32
    timespec resq 2             ; [0] = tv_sec, [1] = tv_nsec

section .text

; ═════════════════════════════════════════════════════════════════
; get_time() → rax
;
;   Returns the current Unix timestamp (seconds since 1970-01-01).
;   clock_gettime(CLOCK_REALTIME) fills timespec; tv_sec is
;   already the Unix epoch second count so no conversion is needed.
; ═════════════════════════════════════════════════════════════════
get_time:
    mov     rax, SYS_CLOCK_GETTIME
    mov     rdi, CLOCK_REALTIME
    lea     rsi, [rel timespec]
    syscall

    mov     rax, [rel timespec]     ; tv_sec
    ret


; ═════════════════════════════════════════════════════════════════
; mem_alloc(size_t size) → rax
;   rdi = number of bytes to allocate
;
;   Uses mmap with an 8-byte size header so mem_free can recover
;   the length for munmap. Returns ptr+8 to the caller, matching
;   the same opaque-pointer contract as the Windows version.
; ═════════════════════════════════════════════════════════════════
mem_alloc:
    push    rbx
    mov     rbx, rdi

    mov     rax, SYS_MMAP
    xor     rdi, rdi
    lea     rsi, [rbx + 8]
    mov     rdx, PROT_READ | PROT_WRITE
    mov     r10, MAP_PRIVATE | MAP_ANONYMOUS
    mov     r8,  -1
    xor     r9,  r9
    syscall

    test    rax, rax
    js      .mem_alloc_done
    mov     [rax], rbx          ; store requested size in header
    add     rax, 8              ; return pointer past header

.mem_alloc_done:
    pop     rbx
    ret


; ═════════════════════════════════════════════════════════════════
; mem_free(void* ptr)
;   rdi = pointer previously returned by mem_alloc
; ═════════════════════════════════════════════════════════════════
mem_free:
    push    rbx
    mov     rbx, rdi

    sub     rbx, 8              ; step back to size header
    mov     rsi, [rbx]          ; recover original requested size
    add     rsi, 8              ; total mapping length including header

    mov     rax, SYS_MUNMAP
    mov     rdi, rbx
    syscall

    pop     rbx
    ret


; ═════════════════════════════════════════════════════════════════
; print_char(char* c)
;   rdi = pointer to a single character to print
; ═════════════════════════════════════════════════════════════════
print_char:
    push    rbx
    mov     rbx, rdi

    mov     al, [rbx]
    mov     [rel char_buf], al

    mov     rax, SYS_WRITE
    mov     rdi, STDOUT_FD
    lea     rsi, [rel char_buf]
    mov     rdx, 1
    syscall

    mov     eax, 1

    pop     rbx
    ret


; ═════════════════════════════════════════════════════════════════
; print_text(const char* s) → rax
;   rdi = pointer to null-terminated string
; ═════════════════════════════════════════════════════════════════
print_text:
    push    rbx
    push    r12

    mov     rbx, rdi
    xor     r12d, r12d

.print_text_loop:
    movzx   eax, byte [rbx]
    test    al, al
    jz      .print_text_done

    mov     [rel char_buf], al

    mov     rax, SYS_WRITE
    mov     rdi, STDOUT_FD
    lea     rsi, [rel char_buf]
    mov     rdx, 1
    syscall

    inc     r12d
    inc     rbx
    jmp     .print_text_loop

.print_text_done:
    mov     eax, r12d

    pop     r12
    pop     rbx
    ret


; ═════════════════════════════════════════════════════════════════
; read_text(char* buf, int size)
;   rdi = pointer to caller-supplied buffer
;   rsi = capacity of the buffer in bytes
;
;   Strips the trailing \n (0x0A) that Linux line input includes.
; ═════════════════════════════════════════════════════════════════
read_text:
    push    rbx
    push    r12

    mov     rbx, rdi
    mov     r12, rsi

    mov     rax, SYS_READ
    mov     rdi, STDIN_FD
    mov     rsi, rbx
    mov     rdx, r12
    syscall

    mov     rsi, rbx
.read_text_find_lf:
    movzx   eax, byte [rsi]
    test    al, al
    jz      .read_text_done
    cmp     al, 0x0A
    je      .read_text_terminate
    inc     rsi
    jmp     .read_text_find_lf
.read_text_terminate:
    mov     byte [rsi], 0
.read_text_done:
    pop     r12
    pop     rbx
    ret


; ═════════════════════════════════════════════════════════════════
; print_int(long long num) → rax
;   rdi = integer to print (signed 64-bit)
; ═════════════════════════════════════════════════════════════════
print_int:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15

    mov     rbx, rdi

    lea     r12, [rel int_buf]
    mov     byte [r12+31], 0
    mov     r13, 30

    xor     r14, r14
    test    rbx, rbx
    jns     .print_int_convert
    mov     r14, 1
    neg     rbx

.print_int_convert:
    mov     rax, rbx
    mov     rcx, 10
.print_int_digit_loop:
    xor     rdx, rdx
    div     rcx
    add     dl, '0'
    mov     [r12+r13], dl
    dec     r13
    test    rax, rax
    jnz     .print_int_digit_loop

    test    r14, r14
    jz      .print_int_write
    mov     byte [r12+r13], '-'
    dec     r13

.print_int_write:
    inc     r13

    mov     r15, 31
    sub     r15, r13

    mov     rax, SYS_WRITE
    mov     rdi, STDOUT_FD
    lea     rsi, [r12+r13]
    mov     rdx, r15
    syscall

    mov     rax, r15

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret
