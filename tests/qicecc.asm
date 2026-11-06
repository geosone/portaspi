; QICECC.COM - runs PORTASPI's floppy tape Reed-Solomon code (src/qicecc.inc)
; on the segments of VEC.BIN, which tests/qictape.py --ecc-vectors writes:
; each an 8 byte header (n good sectors, the number of erasures, their
; three places, 3 bytes unused) and the n sectors, damaged.  It prints
;   ECC i r crc
; r 0 if the segment was put right (crc: the CRC-32 of its n sectors),
; 1 if it was found damaged beyond repair; then QICECC DONE.
; Test tool of tests/dos_86box_qicecc.sh.  nasm -f bin -Isrc/ -o QICECC.COM
; License: GNU General Public License version 2 or later.

        cpu     486
        bits    16
        org     100h

start:  cld
        call    qt_gf_init
        call    crc_init
        mov     dx, vec_name
        mov     ax, 3D00h
        int     21h
        jc      .nofile
        mov     [handle], ax
.next:  mov     bx, [handle]            ; the header
        mov     ah, 3Fh
        mov     cx, 8
        mov     dx, hdr
        int     21h
        jc      .done
        cmp     ax, 8
        jne     .done
        movzx   cx, byte [hdr]          ; n sectors
        shl     cx, 10
        mov     ah, 3Fh
        mov     dx, buf
        int     21h
        jc      .done
        mov     ax, [hdr]               ; n, erasures
        movzx   bx, al
        sub     bx, 3
        shl     bx, 10
        mov     [qt_bcap], bx
        mov     [qt_nera], ah
        mov     ax, [hdr+2]
        mov     [qt_era], ax
        mov     al, [hdr+4]
        mov     [qt_era+2], al
        call    qt_ecc
        setc    [result]
        mov     dx, msg_ecc
        call    puts
        mov     eax, [index]
        call    putdec
        mov     al, ' '
        call    putc
        movzx   eax, byte [result]
        call    putdec
        mov     al, ' '
        call    putc
        movzx   ecx, byte [hdr]
        shl     ecx, 10
        mov     si, buf
        mov     dword [crc], 0FFFFFFFFh
        call    crc_add
        mov     eax, [crc]
        not     eax
        call    puthex8
        call    crlf
        inc     dword [index]
        jmp     .next
.done:  mov     dx, msg_done
        call    puts
        mov     ax, 4C00h
        int     21h
.nofile:
        mov     dx, msg_nofile
        call    puts
        mov     ax, 4C01h
        int     21h

%include "qicecc.inc"

qt_buf          dw      buf
qt_bcap         dw      0

;--- CRC-32 -----------------------------------------------------------------
crc_init:
        xor     bx, bx
.t:     movzx   eax, bl
        mov     cx, 8
.b:     shr     eax, 1
        jnc     .n
        xor     eax, 0EDB88320h
.n:     loop    .b
        mov     di, bx
        shl     di, 2
        mov     [crc_tab+di], eax
        inc     bl
        jnz     .t
        ret

; ECX bytes at DS:SI
crc_add:
        jecxz   .o
        mov     eax, [crc]
.l:     movzx   bx, al
        xor     bl, [si]
        inc     si
        shl     bx, 2
        shr     eax, 8
        xor     eax, [crc_tab+bx]
        dec     ecx
        jnz     .l
        mov     [crc], eax
.o:     ret

;--- output -------------------------------------------------------------------
puts:   mov     ah, 9
        int     21h
        ret

crlf:   mov     dx, msg_crlf
        jmp     puts

putc:   push    dx
        mov     dl, al
        mov     ah, 2
        int     21h
        pop     dx
        ret

putdec: mov     ebx, 10                 ; EAX
        xor     cx, cx
.d:     xor     edx, edx
        div     ebx
        push    dx
        inc     cx
        or      eax, eax
        jnz     .d
.p:     pop     ax
        add     al, '0'
        call    putc
        loop    .p
        ret

puthex8:
        mov     cx, 8                   ; EAX, eight digits
.h:     rol     eax, 4
        push    eax
        and     al, 0Fh
        add     al, '0'
        cmp     al, '9'
        jbe     .p
        add     al, 7
.p:     call    putc
        pop     eax
        loop    .h
        ret

vec_name        db      'VEC.BIN', 0
msg_ecc         db      'ECC $'
msg_done        db      'QICECC DONE', 13, 10, '$'
msg_nofile      db      'QICECC: no VEC.BIN', 13, 10, '$'
msg_crlf        db      13, 10, '$'

handle          dw      0
index           dd      0
crc             dd      0
result          db      0
hdr             times 8 db 0

crc_tab         times 256 dd 0
buf:
