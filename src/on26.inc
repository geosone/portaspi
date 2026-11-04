;=============================================================================
; on26.inc - the OnSpec 90c26 parallel port to IDE bridge (ValuStore and
; other parallel port hard disks and CD-ROMs)
;
; The sequences are those of Linux drivers/block/paride/on26.c (Grant
; Guenther), modes 0 (4-bit: reads in nibbles on the status lines) and 1
; (8-bit: reads on the data lines).  The bridge has an index register
; (P1: SELECT pulses with STROBE) and writes the data lines into the
; register it names (P2: an AUTOFD pulse with STROBE).  Register 1 holds
; the IDE address, register 2 the mode, register 0 is the window onto the
; IDE register: two bytes, low first, make one 16-bit IDE cycle.
;
; Behind it is an ATAPI drive (atapi_exec) or an ATA disk (ata_exec).
;
; License: GNU General Public License version 2 only.
;=============================================================================

on_rmode        db      08h             ; mode register: 08h 4-bit, 09h 8-bit

; P1: latch the data lines as the index
on_p1:  push    ax
        CTR     05h
        CTR     0Dh
        CTR     05h
        CTR     0Dh
        CTR     05h
        CTR     04h
        pop     ax
        ret

; P2: write the data lines into the register indexed
on_p2:  push    ax
        CTR     05h
        CTR     07h
        CTR     05h
        CTR     04h
        pop     ax
        ret

; internal register AH = AL
on_set: push    ax
        mov     al, ah
        out     dx, al
        call    on_p1
        pop     ax
        out     dx, al
        jmp     on_p2

; CCP(x): the command packet AL
on_cpp: push    si
        push    ax
        mov     si, on_cpp_seq
.l:     mov     al, [si]
        out     dx, al
        inc     si
        cmp     si, on_cpp_seq + 7
        jb      .l
        pop     ax
        out     dx, al
        CTR     04h
        CTR     05h
        CTR     04h
        mov     al, 0FFh
        out     dx, al
        pop     si
        ret

on_cpp_seq      db      0FEh, 0AAh, 55h, 00h, 0FFh, 87h, 78h

; the window onto IDE register AH (AH bit 7: the control block)
on_addr:
        push    ax
        mov     al, ah
        and     al, 07h
        shl     al, 2
        inc     al                      ; (reg << 2) + 1 + cont
        test    ah, 80h
        jz      .c
        inc     al
.c:     mov     ah, 1
        call    on_set
        xor     al, al
        out     dx, al
        call    on_p1
        pop     ax
        ret

; on26_write_regr(): IDE register AH = AL
on26_wr:
        call    on_addr
        out     dx, al
        call    on_p2
        out     dx, al
        jmp     on_p2

; on26_read_regr(): AL = IDE register AH
on26_rd:
        call    on_addr
        push    bx
        cmp     byte [lpt_mode], MODE_PS2
        je      .ps2
        CTR     06h
        inc     dx
        in      al, dx
        dec     dx
        mov     bl, al
        CTR     04h
        CTR     06h
        inc     dx
        in      al, dx
        dec     dx
        mov     bh, al
        CTR     04h
        CTR     06h
        CTR     04h
        CTR     06h
        CTR     04h
        mov     al, bl                  ; j44(a, b)
        shr     al, 4
        and     bh, 0F0h
        or      al, bh
        pop     bx
        ret
.ps2:   CTR     26h
        in      al, dx
        mov     bl, al
        CTR     04h
        CTR     26h
        CTR     04h
        mov     al, bl
        pop     bx
        ret

; on26_connect()
on26_connect:
        push    ax
        mov     al, 20h
        call    on_cpp
        mov     ax, 0208h
        call    on_set
        mov     ah, 2
        mov     al, [on_rmode]
        call    on_set
        pop     ax
        ret

; on26_disconnect()
on26_disconnect:
        push    ax
        mov     al, 4
        out     dx, al
        call    on_p1
        mov     al, 4
        out     dx, al
        call    on_p1
        mov     al, 30h
        call    on_cpp
        pop     ax
        ret

; the window onto the IDE data register in block mode
on_block:
        mov     ax, 0101h
        call    on_set
        mov     ah, 2
        mov     al, [on_rmode]
        or      al, 10h
        call    on_set
        xor     al, al
        out     dx, al
        jmp     on_p1

on_unblock:
        mov     ah, 2
        mov     al, [on_rmode]
        jmp     on_set

; on26_read_block(): CX bytes to ES:DI (even in 8-bit mode)
on26_get:
        push    bx
        call    on_block
        cmp     byte [lpt_mode], MODE_PS2
        je      .ps2
.n:     CTR     06h
        inc     dx
        in      al, dx
        mov     bl, al
        dec     dx
        CTR     04h
        inc     dx
        in      al, dx
        dec     dx
        shr     bl, 4
        and     al, 0F0h
        or      al, bl
        stosb
        loop    .n
        jmp     .end
.ps2:   inc     cx
        shr     cx, 1
.b:     CTR     26h
        in      al, dx
        stosb
        CTR     24h
        in      al, dx
        stosb
        loop    .b
        CTR     04h
.end:   call    on_unblock
        pop     bx
        ret

; on26_write_block(): CX bytes from ES:DI (pairs)
on26_put:
        call    on_block
        inc     cx
        shr     cx, 1
.l:     CTR     05h
        mov     al, [es:di]
        out     dx, al
        CTR     07h
        mov     al, [es:di+1]
        out     dx, al
        add     di, 2
        loop    .l
        CTR     05h
        CTR     04h
        jmp     on_unblock

;-----------------------------------------------------------------------------
; on26_init: an OnSpec 90c26 on lpt_base?  As on26_test_port(): the command
; packet must answer B, 5, F in the high nibble of the status, then the
; drive behind it is reset and must come ready.  CF if there is none.
; -> dev_ata: 1 for an ATA disk, 0 for an ATAPI drive
;-----------------------------------------------------------------------------
on26_init:
        mov     dx, [lpt_base]
        mov     byte [on_rmode], 08h
        cmp     byte [lpt_mode], MODE_PS2
        jne     .m
        mov     byte [on_rmode], 09h
.m:     CTR     0Ch
        mov     al, 30h
        call    on_cpp
        xor     al, al
        call    on_cpp
        mov     si, on_cpp_seq          ; FE AA 55 00 FF
.s:     lodsb
        out     dx, al
        cmp     si, on_cpp_seq + 5
        jb      .s
        inc     dx
        in      al, dx
        dec     dx
        mov     bl, al
        mov     al, 87h
        out     dx, al
        inc     dx
        in      al, dx
        dec     dx
        mov     bh, al
        mov     al, 78h
        out     dx, al
        mov     al, 20h                 ; connect
        out     dx, al
        CTR     04h
        CTR     05h
        inc     dx
        in      al, dx
        dec     dx
        mov     cl, al
        CTR     04h
        mov     al, 0FFh
        out     dx, al
        and     bl, 0F0h
        cmp     bl, 0B0h
        jne     .no
        and     bh, 0F0h
        cmp     bh, 50h
        jne     .no
        and     cl, 0F0h
        cmp     cl, 0F0h
        jne     .no

        ; reset the drive with the bridge's reset line
        mov     ax, 0200h
        call    on_set
        mov     ax, 0300h
        call    on_set
        mov     ax, 0208h
        call    on_set
        mov     cx, 7                   ; 100 us
        call    delay15
        mov     ax, 020Ah
        call    on_set
        mov     cx, 7
        call    delay15
        mov     ax, 0208h
        call    on_set
        mov     cx, 67                  ; 1 ms
        call    delay15
        mov     ah, 2
        mov     al, [on_rmode]
        call    on_set
        mov     byte [lpt_chip], CHIP_ON26
        mov     ax, (TF_DRVHD << 8) | 0A0h
        call    tf_wr
        mov     ebx, 18 * 20            ; 20 s for the drive to spin up
        call    atapi_wait
        jc      .gone
        cmp     al, 0FFh                ; nothing on the IDE bus
        je      .gone
        mov     ah, TF_BCLO             ; the signature: 14h EBh is ATAPI
        call    tf_rd
        mov     bl, al
        mov     ah, TF_BCHI
        call    tf_rd
        mov     bh, al
        mov     byte [dev_ata], 0
        cmp     bx, 0EB14h
        je      .ok
        mov     byte [dev_ata], 1
        call    ata_probe               ; IDENTIFY DEVICE
        jc      .gone
.ok:    call    on26_disconnect
        clc
        ret
.gone:  call    on26_disconnect
.no:    mov     al, 30h
        call    on_cpp
        stc
        ret
