global get_time
global mem_alloc
global mem_free
global print_text
global read_text
global print_char
global print_int

extern GetSystemTimeAsFileTime
extern GetProcessHeap
extern HeapAlloc
extern HeapFree
extern GetStdHandle
extern WriteConsoleA
extern ReadConsoleA

; ─────────────────────────────────────────────────────────────────
; CONSTANTS
; ─────────────────────────────────────────────────────────────────
STD_OUTPUT_HANDLE equ -11
STD_INPUT_HANDLE  equ -10

; ─────────────────────────────────────────────────────────────────
; UNINITIALISED DATA
; ─────────────────────────────────────────────────────────────────
section .bss
    hStdOut  resq 1
    hStdIn   resq 1
    char_buf resb 1
    int_buf  resb 32

section .text

; ═════════════════════════════════════════════════════════════════
; get_time() → rax
;
;   Returns the current Unix timestamp (seconds since 1970-01-01)
;   as an unsigned 64-bit integer.
;
;   Stack layout (after push rbx, sub rsp 48):
;     [rsp+32..39]  FILETIME low  dword
;     [rsp+36..39]  FILETIME high dword
;     [rsp+ 0..31]  shadow space for callees
; ═════════════════════════════════════════════════════════════════
get_time:
    push    rbx
    sub     rsp, 48

    lea     rcx, [rsp+32]
    call    GetSystemTimeAsFileTime

    mov     eax, [rsp+32]
    mov     edx, [rsp+36]
    shl     rdx, 32
    or      rax, rdx

    mov     rcx, 116444736000000000
    sub     rax, rcx

    mov     rcx, 10000000
    xor     rdx, rdx
    div     rcx

    add     rsp, 48
    pop     rbx
    ret


; ═════════════════════════════════════════════════════════════════
; mem_alloc(size_t size) → rax
;   rcx = number of bytes to allocate
; ═════════════════════════════════════════════════════════════════
mem_alloc:
    push    rbx
    sub     rsp, 56

    mov     rbx, rcx
    call    GetProcessHeap

    mov     rcx, rax
    xor     rdx, rdx
    mov     r8,  rbx
    call    HeapAlloc

    add     rsp, 56
    pop     rbx
    ret


; ═════════════════════════════════════════════════════════════════
; mem_free(void* ptr)
;   rcx = pointer to block previously returned by mem_alloc
; ═════════════════════════════════════════════════════════════════
mem_free:
    push    rbx
    sub     rsp, 56

    mov     rbx, rcx
    call    GetProcessHeap

    mov     rcx, rax
    xor     rdx, rdx
    mov     r8,  rbx
    call    HeapFree

    add     rsp, 56
    pop     rbx
    ret


; ═════════════════════════════════════════════════════════════════
; print_char(char* c)
;   rcx = pointer to a single character to print
; ═════════════════════════════════════════════════════════════════
print_char:
    push    rbp
    mov     rbp, rsp
    push    rbx
    sub     rsp, 56

    mov     rbx, rcx

    mov     rcx, STD_OUTPUT_HANDLE
    call    GetStdHandle
    mov     [rel hStdOut], rax

    mov     al, [rbx]
    mov     [rel char_buf], al
    mov     rcx, [rel hStdOut]
    lea     rdx, [rel char_buf]
    mov     r8,  1
    lea     r9,  [rsp+32]
    mov     qword [rsp+40], 0
    call    WriteConsoleA
    mov     eax, 1

    add     rsp, 56
    pop     rbx
    pop     rbp
    ret


; ═════════════════════════════════════════════════════════════════
; print_text(const char* s) → rax
;   rcx = pointer to null-terminated string
; ═════════════════════════════════════════════════════════════════
print_text:
    push    rbp
    mov     rbp, rsp
    push    rbx
    sub     rsp, 56

    mov     rbx, rcx
    mov     dword [rsp+40], 0

    mov     rcx, STD_OUTPUT_HANDLE
    call    GetStdHandle
    mov     [rel hStdOut], rax

.print_text_loop:
    movzx   eax, byte [rbx]
    test    al, al
    jz      .print_text_done

    mov     [rel char_buf], al
    mov     rcx, [rel hStdOut]
    lea     rdx, [rel char_buf]
    mov     r8,  1
    lea     r9,  [rsp+32]
    mov     qword [rsp+48], 0
    call    WriteConsoleA

    inc     dword [rsp+40]
    inc     rbx
    jmp     .print_text_loop

.print_text_done:
    mov     eax, [rsp+40]
    add     rsp, 56
    pop     rbx
    pop     rbp
    ret


; ═════════════════════════════════════════════════════════════════
; read_text(char* buf, int size)
;   rcx = pointer to caller-supplied buffer
;   rdx = capacity of the buffer in bytes
; ═════════════════════════════════════════════════════════════════
read_text:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    rdi
    sub     rsp, 48

    mov     rbx, rcx
    mov     rdi, rdx

    mov     rcx, STD_INPUT_HANDLE
    call    GetStdHandle
    mov     [rel hStdIn], rax

    mov     rcx, [rel hStdIn]
    mov     rdx, rbx
    mov     r8,  rdi
    lea     r9,  [rsp+32]
    mov     qword [rsp+40], 0
    call    ReadConsoleA

    mov     rsi, rbx
.read_text_find_cr:
    movzx   eax, byte [rsi]
    test    al, al
    jz      .read_text_done
    cmp     al, 0x0D
    je      .read_text_terminate
    inc     rsi
    jmp     .read_text_find_cr
.read_text_terminate:
    mov     byte [rsi], 0
.read_text_done:
    add     rsp, 48
    pop     rdi
    pop     rbx
    pop     rbp
    ret


; ═════════════════════════════════════════════════════════════════
; print_int(long long num) → rax
;   rcx = integer to print (signed 64-bit)
; ═════════════════════════════════════════════════════════════════
print_int:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    rdi
    push    rsi
    sub     rsp, 40

    mov     rbx, rcx

    mov     rcx, STD_OUTPUT_HANDLE
    call    GetStdHandle
    mov     [rel hStdOut], rax

    lea     rdi, [rel int_buf]
    mov     byte [rdi+31], 0
    mov     rsi, 30

    xor     r8, r8
    test    rbx, rbx
    jns     .print_int_convert
    mov     r8, 1
    neg     rbx

.print_int_convert:
    mov     rax, rbx
    mov     rcx, 10
.print_int_digit_loop:
    xor     rdx, rdx
    div     rcx
    add     dl, '0'
    mov     [rdi+rsi], dl
    dec     rsi
    test    rax, rax
    jnz     .print_int_digit_loop

    test    r8, r8
    jz      .print_int_write
    mov     byte [rdi+rsi], '-'
    dec     rsi

.print_int_write:
    inc     rsi

    mov     r10, 31
    sub     r10, rsi

    mov     rcx, [rel hStdOut]
    lea     rdx, [rdi+rsi]
    mov     r8,  r10
    lea     r9,  [rsp+32]
    mov     qword [rsp+40], 0
    call    WriteConsoleA

    mov     rax, r10

    add     rsp, 40
    pop     rsi
    pop     rdi
    pop     rbx
    pop     rbp
    ret
