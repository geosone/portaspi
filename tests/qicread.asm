; QICREAD.COM - reads every file of the first ASPI tape drive and prints
; each one's size and CRC-32 (as zip and Python's zlib.crc32 compute it):
;   VOL n size crc
; then checks the filemark handling: SPACE back one filemark from the end
; of data and READ (a filemark at once), and SPACE over the first filemark
; from the load point and read the second file again:
;   BACK ok
;   SPACED size crc
; Reads are READ (6) of variable blocks of CHUNK bytes; a filemark comes
; as CHECK CONDITION with FILEMARK and the residue in the information.
; Test tool of tests/dos_86box_qic.sh.  nasm -f bin -o QICREAD.COM
; License: GNU General Public License version 2 only.

        cpu     486
        bits    16
        org     100h

CHUNK   equ     12000                   ; not a multiple of the sector size

start:  cld
        xor     ax, ax                  ; an invalid opcode: say where
        mov     es, ax
        mov     word [es:6*4], int6
        mov     [es:6*4+2], cs
        push    ds
        pop     es
        call    crc_init
        mov     dx, mgr_name            ; the ASPI entry point
        mov     ax, 3D00h
        int     21h
        jc      no_aspi
        mov     bx, ax
        mov     ax, 4402h
        mov     dx, aspi
        mov     cx, 4
        int     21h
        pushf
        mov     ah, 3Eh
        int     21h
        popf
        jc      no_aspi

        mov     byte [ha], 0            ; the first tape drive
.ha:    mov     byte [tgt], 0
.tg:    call    srb_clear
        mov     byte [srb], 1           ; SC_GET_DEV_TYPE
        mov     al, [ha]
        mov     [srb+2], al
        mov     al, [tgt]
        mov     [srb+8], al
        call    aspi_call
        cmp     byte [srb+1], 1
        jne     .nt
        cmp     byte [srb+0Ah], 1       ; sequential access
        je      found
.nt:    inc     byte [tgt]
        cmp     byte [tgt], 7
        jb      .tg
        inc     byte [ha]
        cmp     byte [ha], 4
        jb      .ha
        mov     dx, msg_notape
        jmp     fail

found:  mov     cx, 5                   ; TEST UNIT READY: the UNIT ATTENTION
.tur:   push    cx
        mov     al, 00h
        call    cmd6
        pop     cx
        cmp     byte [srb+19h], 0
        je      .rdy
        loop    .tur
.rdy:   mov     al, 01h                 ; REWIND
        call    cmd6
        call    check_good

        mov     byte [vol], 0
.vol:   call    read_file
        jc      .eod
        mov     dx, msg_vol
        call    puts
        mov     al, [vol]
        movzx   eax, al
        call    putdec
        call    put_size_crc
        inc     byte [vol]
        jmp     .vol
.eod:   mov     dx, msg_eod
        call    puts
        mov     al, [vol]
        movzx   eax, al
        call    putdec
        call    crlf

        cmp     byte [vol], 0           ; no filemark to space over
        je      .nos
        mov     eax, 0FFFFFFh           ; SPACE -1 filemark
        call    space
        call    check_good
        call    read_chunk              ; a filemark at once, nothing read
        cmp     byte [srb+19h], 2
        jne     .bad_back
        test    byte [sense+2], 80h
        jz      .bad_back
        cmp     dword [got], 0
        jne     .bad_back
        mov     dx, msg_back
        call    puts
        jmp     .sp
.bad_back:
        mov     dx, msg_badback
        call    puts
.sp:    mov     al, 01h                 ; REWIND, SPACE 1, the second file
        call    cmd6
        call    check_good
        mov     eax, 1
        call    space
        call    check_good
        call    read_file
        jc      .nos
        mov     dx, msg_spaced
        call    puts
        call    put_size_crc
.nos:   mov     dx, msg_done
        call    puts
        mov     ax, 4C00h
        int     21h

int6:   push    cs
        pop     ds
        mov     bp, sp
        mov     dx, msg_ud
        call    puts
        movzx   eax, word [bp+2]
        call    puthex4
        mov     al, ':'
        call    putc
        movzx   eax, word [bp]
        call    puthex4
        mov     al, ' '
        call    putc
        mov     es, [bp+2]
        mov     si, [bp]
        mov     cx, 8
.b:     movzx   eax, byte [es:si]
        call    puthex
        inc     si
        loop    .b
        call    crlf
        mov     ax, 4C02h
        int     21h

no_aspi:
        mov     dx, msg_noaspi
fail:   call    puts
        mov     ax, 4C01h
        int     21h

; read a file to its filemark: size and crc.  CF at the end of data.
read_file:
        mov     dword [size], 0
        mov     dword [crc], 0FFFFFFFFh
.l:     call    read_chunk
        mov     ecx, [got]
        add     [size], ecx
        mov     si, buf
        call    crc_add
        cmp     byte [srb+19h], 0
        je      .l
        cmp     byte [srb+19h], 2
        jne     .err
        test    byte [sense+2], 80h     ; the filemark
        jnz     .fm
        mov     al, [sense+2]
        and     al, 0Fh
        cmp     al, 8                   ; BLANK CHECK: the end of data
        jne     .err
        stc
        ret
.fm:    clc
        ret
.err:   call    sense_out
        mov     ax, 4C01h
        int     21h

; READ (6) of CHUNK bytes -> got = the bytes read
read_chunk:
        call    srb_clear
        mov     byte [srb+3], 08h       ; in
        mov     dword [srb+0Ah], CHUNK
        mov     word [srb+0Fh], buf
        mov     [srb+11h], ds
        mov     byte [cdb], 08h
        mov     byte [cdb+3], CHUNK >> 8
        mov     byte [cdb+4], CHUNK & 0FFh
        call    exec6
        mov     dword [got], CHUNK
        cmp     byte [srb+19h], 2
        jne     .o
        test    byte [sense], 80h       ; the residue
        jz      .o
        mov     eax, [sense+3]
        bswap   eax
        sub     [got], eax
.o:     ret

; SPACE EAX (24 bits) filemarks
space:  push    eax
        call    srb_clear
        pop     eax
        mov     byte [srb+3], 18h       ; no data
        mov     byte [cdb], 11h
        mov     byte [cdb+1], 1
        mov     [cdb+4], al
        mov     [cdb+3], ah
        shr     eax, 16
        mov     [cdb+2], al
        jmp     exec6

; a six byte command AL without data
cmd6:   push    ax
        call    srb_clear
        pop     ax
        mov     [cdb], al
        mov     byte [srb+3], 18h
exec6:  mov     byte [srb], 2           ; SC_EXEC_SCSI_CMD
        mov     al, [ha]
        mov     [srb+2], al
        mov     al, [tgt]
        mov     [srb+8], al
        mov     byte [srb+0Eh], 18      ; sense length
        mov     byte [srb+17h], 6
        jmp     aspi_call

check_good:
        cmp     byte [srb+1], 1
        je      .ok
        call    sense_out
        mov     ax, 4C01h
        int     21h
.ok:    ret

; "ERR status key asc ascq"
sense_out:
        mov     dx, msg_err
        call    puts
        movzx   eax, byte [srb+1]
        call    puthex
        movzx   eax, byte [srb+19h]
        call    puthex
        mov     al, [sense+2]
        and     eax, 0Fh
        call    puthex
        movzx   eax, byte [sense+12]
        call    puthex
        movzx   eax, byte [sense+13]
        call    puthex
        jmp     crlf

srb_clear:
        push    di
        push    es
        push    ds
        pop     es
        mov     di, srb
        mov     cx, srb_end - srb
        xor     al, al
        rep     stosb
        pop     es
        pop     di
        ret

aspi_call:
        push    ds
        push    srb
        call    far [aspi]
        add     sp, 4
.w:     cmp     byte [srb+1], 0         ; pending
        je      .w
        ret

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
put_size_crc:
        mov     al, ' '
        call    putc
        mov     eax, [size]
        call    putdec
        mov     al, ' '
        call    putc
        mov     eax, [crc]
        not     eax
        call    puthex8
        jmp     crlf

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

puthex: push    eax                     ; " xx"
        mov     al, ' '
        call    putc
        pop     eax
        mov     cx, 2
        rol     eax, 24
        jmp     hexd
puthex4:
        rol     eax, 16
        mov     cx, 4
        jmp     hexd
puthex8:
        mov     cx, 8                   ; EAX, eight digits
hexd:   rol     eax, 4
        push    eax
        and     al, 0Fh
        add     al, '0'
        cmp     al, '9'
        jbe     .p
        add     al, 7
.p:     call    putc
        pop     eax
        loop    hexd
        ret

mgr_name        db      'SCSIMGR$', 0
msg_noaspi      db      'QICREAD: no ASPI manager', 13, 10, '$'
msg_notape      db      'QICREAD: no tape drive', 13, 10, '$'
msg_vol         db      'VOL $'
msg_eod         db      'EOD $'
msg_back        db      'BACK ok', 13, 10, '$'
msg_badback     db      'BACK FAILED', 13, 10, '$'
msg_spaced      db      'SPACED$'
msg_err         db      'ERR$'
msg_ud          db      'QICREAD: invalid opcode at $'
msg_done        db      'QICREAD DONE', 13, 10, '$'
msg_crlf        db      13, 10, '$'

aspi            dd      0
ha              db      0
tgt             db      0
vol             db      0
size            dd      0
crc             dd      0
got             dd      0

srb             times 40h db 0
cdb             times 6 db 0
sense           times 18 db 0
srb_end:
crc_tab         times 256 dd 0
buf:
