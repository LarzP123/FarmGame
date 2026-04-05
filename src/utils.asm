global abs_val
global copy_string
global mem_set
global get_rand
global set_rand
global text_to_int
global has_newline
global int_to_str

; ─────────────────────────────────────────────────────────────────
; INITIALISED DATA
;   seed    — current LCG state, starts at the multiplier value
;   lcg_inc — additive constant for the LCG step
; ─────────────────────────────────────────────────────────────────
section .data
    seed    dq 6364136223846793005
    lcg_inc dq 1442695040888963407

; ─────────────────────────────────────────────────────────────────
; UNINITIALISED DATA
;   int_buf — 32 byte scratch buffer for int_to_str digit assembly
; ─────────────────────────────────────────────────────────────────
section .bss
    int_buf resb 32

section .text

; ═════════════════════════════════════════════════════════════════
; abs_val(int x) -> eax  (branchless absolute value)
;
;   Windows: rcx = x
;   Linux:   rdi = x  (ecx is aliased below — caller passes via
;            their ABI register but the body only reads ecx which
;            on Linux will be the 4th arg register; callers on
;            Linux must move rdi into rcx before calling, OR this
;            file is assembled with a shim. For simplicity we keep
;            the body identical and note the ABI difference here.)
;
;   NOTE: All functions in this file use the Windows x64 ABI
;   register names in their bodies (rcx, rdx, r8, r9 for args).
;   The OS-specific files provide thin shims where needed, or
;   callers written for the matching ABI call directly.
;
;   Actually — to keep this file truly shared and callable from
;   both ABIs without shims, all of these functions are
;   self-contained with no inter-function calls and read their
;   single argument from a register that is caller-set. We
;   document the expected register per platform in each header.
; ═════════════════════════════════════════════════════════════════

; ═════════════════════════════════════════════════════════════════
; abs_val(int x) -> eax  (branchless absolute value)
;   Windows: ecx = x
;   Linux:   edi = x
; ═════════════════════════════════════════════════════════════════
abs_val:
%ifdef LINUX
    mov     eax, edi
%else
    mov     eax, ecx
%endif
    mov     edx, eax
    sar     edx, 31
    add     eax, edx
    xor     eax, edx
    ret


; ═════════════════════════════════════════════════════════════════
; copy_string(char* d, char* s)
;   Windows: rcx = d, rdx = s
;   Linux:   rdi = d, rsi = s
; ═════════════════════════════════════════════════════════════════
copy_string:
%ifdef LINUX
    ; rdi = d, rsi = s — use r8/r9 as working pointers to avoid
    ; clobbering the ABI argument registers in case of future use
    mov     r8,  rdi
    mov     r9,  rsi
%else
    mov     r8,  rcx
    mov     r9,  rdx
%endif
.copy_string_loop:
    mov     al,  [r9]
    mov     [r8], al
    inc     r9
    inc     r8
    test    al, al
    jne     .copy_string_loop
    ret


; ═════════════════════════════════════════════════════════════════
; mem_set(void* dst, int val, unsigned int n)
;   Windows: rcx = dst, rdx = val, r8d = n
;   Linux:   rdi = dst, rsi = val, rdx = n
; ═════════════════════════════════════════════════════════════════
mem_set:
%ifdef LINUX
    test    edx, edx
    jz      .mem_set_done
    movzx   eax, sil
.mem_set_loop:
    mov     [rdi], al
    inc     rdi
    dec     edx
    jnz     .mem_set_loop
%else
    test    r8d, r8d
    jz      .mem_set_done
    movzx   eax, dl
.mem_set_loop:
    mov     [rcx], al
    inc     rcx
    dec     r8d
    jnz     .mem_set_loop
%endif
.mem_set_done:
    ret


; ═════════════════════════════════════════════════════════════════
; set_rand(unsigned long seed_val)
;   Windows: rcx = seed_val
;   Linux:   rdi = seed_val
; ═════════════════════════════════════════════════════════════════
set_rand:
%ifdef LINUX
    mov     [rel seed], rdi
%else
    mov     [rel seed], rcx
%endif
    ret


; ═════════════════════════════════════════════════════════════════
; get_rand() → rax
; (no arguments — identical on both platforms)
; ═════════════════════════════════════════════════════════════════
get_rand:
    mov     rax, [rel seed]
    mov     rcx, 6364136223846793005
    imul    rax, rcx
    add     rax, [rel lcg_inc]
    mov     [rel seed], rax
    shr     rax, 33
    ret


; ═════════════════════════════════════════════════════════════════
; text_to_int(char* buf, int* out) → rax
;   Windows: rcx = buf, rdx = out
;   Linux:   rdi = buf, rsi = out
; ═════════════════════════════════════════════════════════════════
text_to_int:
    push    rbx
    push    r12

%ifdef LINUX
    mov     rbx, rdi
    mov     r12, rsi
%else
    mov     rbx, rcx
    mov     r12, rdx
%endif
    xor     ecx, ecx            ; accumulator
    xor     r8d, r8d            ; negative flag
    xor     r9d, r9d            ; digit count

    movzx   eax, byte [rbx]
    cmp     al, '-'
    jne     .text_to_int_digits
    mov     r8d, 1
    inc     rbx

.text_to_int_digits:
    movzx   eax, byte [rbx]
    cmp     al, '0'
    jl      .text_to_int_done
    cmp     al, '9'
    jg      .text_to_int_done
    sub     al, '0'
    imul    ecx, ecx, 10
    add     ecx, eax
    inc     r9d
    inc     rbx
    jmp     .text_to_int_digits

.text_to_int_done:
    test    r9d, r9d
    jz      .text_to_int_fail

    test    r8d, r8d
    jz      .text_to_int_store
    neg     ecx

.text_to_int_store:
    mov     [r12], ecx
    mov     eax, 1
    jmp     .text_to_int_ret

.text_to_int_fail:
    xor     eax, eax

.text_to_int_ret:
    pop     r12
    pop     rbx
    ret


; ═════════════════════════════════════════════════════════════════
; has_newline(char* buf, int len) → rax
;   Windows: rcx = buf, edx = len
;   Linux:   rdi = buf, esi = len
; ═════════════════════════════════════════════════════════════════
has_newline:
%ifdef LINUX
    mov     rcx, rdi
    mov     edx, esi
%endif
    xor     eax, eax

.loop:
    cmp     eax, edx
    jge     .not_found

    mov     r8b, [rcx + rax]
    cmp     r8b, 10
    je      .found

    inc     eax
    jmp     .loop

.found:
    mov     eax, 1
    ret

.not_found:
    xor     eax, eax
    ret


; ═════════════════════════════════════════════════════════════════
; int_to_str(long long num, char* buf)
;   Windows: rcx = num, rdx = buf
;   Linux:   rdi = num, rsi = buf
; ═════════════════════════════════════════════════════════════════
int_to_str:
    push    rbx
    push    r12
    push    r13

%ifdef LINUX
    movsx   rbx, edi
    mov     r12, rsi
%else
    movsx   rbx, ecx
    mov     r12, rdx
%endif

    lea     r13, [rel int_buf]
    mov     byte [r13+31], 0
    mov     rcx, 30

    xor     r8, r8
    test    rbx, rbx
    jns     .convert
    mov     r8, 1
    neg     rbx

.convert:
    mov     rax, rbx
    mov     rbx, 10
.digit_loop:
    xor     rdx, rdx
    div     rbx
    add     dl, '0'
    mov     [r13+rcx], dl
    dec     rcx
    test    rax, rax
    jnz     .digit_loop

    test    r8, r8
    jz      .copy
    mov     byte [r13+rcx], '-'
    dec     rcx

.copy:
    inc     rcx
    lea     r9, [r13+rcx]       ; source: start of number string

    ; call copy_string with correct ABI
%ifdef LINUX
    mov     rdi, r12            ; dst
    mov     rsi, r9             ; src
%else
    mov     rcx, r12            ; dst
    mov     rdx, r9             ; src
%endif
    call    copy_string

    pop     r13
    pop     r12
    pop     rbx
    ret
