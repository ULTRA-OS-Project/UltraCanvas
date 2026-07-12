; UltraCanvas x86 Assembly sample (NASM syntax, 32-bit Linux / i386)
; Prints a greeting, then counts down from 5 to 1 using a loop.
; Showcases UltraCanvasTextArea x86 syntax highlighting:
;   instructions, registers, directives/keywords, strings, numbers, ';' comments.

        global  _start

        section .data
greeting        db  "UltraCanvas x86 demo", 10, 0
greeting_len    equ $ - greeting
digit           db  "0", 10          ; single digit + newline

        section .bss
scratch         resb 16              ; reserve 16 bytes of scratch space

        section .text
_start:
        ; --- write the greeting to stdout (fd 1) ---
        mov     eax, 4               ; sys_write
        mov     ebx, 1               ; stdout
        mov     ecx, greeting        ; buffer
        mov     edx, greeting_len    ; length
        int     0x80

        ; --- countdown loop: ecx = 5, 4, 3, 2, 1 ---
        mov     ecx, 5
.next:
        push    ecx                  ; preserve counter across the call
        mov     al, cl
        add     al, '0'              ; convert to ASCII digit
        mov     [digit], al

        mov     eax, 4               ; sys_write
        mov     ebx, 1
        mov     ecx, digit
        mov     edx, 2               ; digit + newline
        int     0x80

        pop     ecx
        loop    .next                ; dec ecx, jump while non-zero

        ; --- exit(0) ---
        mov     eax, 1               ; sys_exit
        xor     ebx, ebx
        int     0x80
