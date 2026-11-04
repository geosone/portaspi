;=============================================================================
; fc.inc - the Freecom parallel port to IDE bridges: the Power adapter
; (Xilinx) and the IQ cable (ASIC), Freecom's parallel port CD-ROM, LS-120
; and hard disk drives
;
; The chip sequences are those of Linux drivers/block/paride/frpw.c and
; friq.c (Grant Guenther): mode 0 (register and block reads in nibbles) and
; mode 1 (8-bit reads, the port reversed), writes in bytes.  Both take a
; command byte that selects a register; they differ in how it is announced:
; the Power adapter latches it with SELECT and AUTOFD, the IQ cable takes it
; after the data lines went through FF 73 C9 26.
;
; License: GNU General Public License version 2 only.
;=============================================================================

FC_TF           equ     08h             ; frpw.c/friq.c cont_map[0]
FC_DEVCTL       equ     16h             ; cont_map[1] + 6
FC_TEST         equ     10h             ; block reads give 0, 1, 2, ...
FC_PS2          equ     0C0h            ; mode 1 block reads
FC_END          equ     20h             ; the disconnect
FQ_POWER        equ     3Dh             ; IQ: turn the drive's power on

;-----------------------------------------------------------------------------
; Bridge access (DX = base)
;-----------------------------------------------------------------------------
; the command byte AL: frpw.c "w0(r); cec4", friq.c CMD(r)
fc_cmd:
        push    ax
        push    si
        mov     ah, al
        CTR     04h
        cmp     byte [lpt_chip], CHIP_FRIQ
        jne     .pw
        mov     si, fq_prefix
.q:     lodsb
        out     dx, al
        out     dx, al
        cmp     si, fq_prefix + 4
        jb      .q
        mov     al, ah
        out     dx, al
        out     dx, al
        jmp     .out
.pw:    mov     al, ah
        out     dx, al
        mov     si, fp_cec4
        add     dx, 2
.c:     lodsb
        out     dx, al
        cmp     si, fp_cec4 + 7
        jb      .c
        sub     dx, 2
.out:   pop     si
        pop     ax
        ret

fq_prefix       db      0FFh, 73h, 0C9h, 26h
fp_cec4         db      0Ch, 0Eh, 0Eh, 0Ch, 04h, 04h, 04h

; frpw_write_regr()/friq_write_regr(): command AH = AL
fc_wr:
        push    ax
        push    ax
        mov     al, ah
        call    fc_cmd
        pop     ax
        out     dx, al
        CTR     05h
        CTR     07h
        CTR     05h
        CTR     04h
        pop     ax
        ret

; frpw_read_regr()/friq_read_regr(): AL = command AH
fc_rd:
        push    bx
        mov     al, ah
        call    fc_cmd
        CTR     06h
        inc     dx
        in      al, dx                  ; low nibble in the high half
        mov     bl, al
        inc     dx
        mov     al, 04h
        out     dx, al
        dec     dx
        in      al, dx                  ; high nibble
        dec     dx
        and     al, 0F0h
        shr     bl, 4
        or      al, bl
        pop     bx
        ret

fc_connect:
        push    ax
        CTR     04h
        pop     ax
        ret

fc_disconnect:
        push    ax
        mov     al, FC_END
        call    fc_cmd
        pop     ax
        ret

; read_block_int(), mode 0 or 1: CX bytes of command AL to ES:DI (CX > 0)
fc_get:
        mov     al, FC_TF
fc_getr:
        push    bx
        cmp     byte [lpt_mode], MODE_PS2
        je      .ps2
        call    fc_cmd
.n:     CTR     06h
        inc     dx
        in      al, dx                  ; low nibble
        mov     bl, al
        inc     dx
        mov     al, 04h
        out     dx, al
        dec     dx
        in      al, dx                  ; high nibble
        dec     dx
        and     al, 0F0h
        shr     bl, 4
        or      al, bl
        stosb
        loop    .n
        pop     bx
        ret
.ps2:   add     al, FC_PS2
        call    fc_cmd
        mov     al, 0FFh
        out     dx, al
        mov     bl, 2                   ; ph
.b:     add     dx, 2
        mov     al, 0A4h
        or      al, bl
        out     dx, al
        sub     dx, 2
        in      al, dx
        stosb
        xor     bl, 2
        loop    .b
        CTR     0ACh
        CTR     0A4h
        CTR     04h
        pop     bx
        ret

; write_block(), modes 0 and 1: CX bytes from ES:DI (CX > 0)
fc_put:
        mov     al, FC_TF
        call    fc_cmd
        CTR     05h
.l:     mov     al, [es:di]
        inc     di
        out     dx, al
        CTR     07h
        CTR     05h
        loop    .l
        CTR     04h
        ret

;-----------------------------------------------------------------------------
; fc_init: a Freecom bridge with a drive on lpt_base?  First the IQ cable
; (its drive's power turned on), then the Power adapter: the test pattern
; (test_proto()'s block read of 10h) and the task file (registers 2 and 3
; written, 2 read back) must come back.  Then a soft reset through the
; device control register and the signature tell an ATAPI drive (14h EBh)
; from an ATA disk.  CF if none.
; -> lpt_chip CHIP_FRIQ or CHIP_FRPW, dev_ata: 1 for an ATA disk
;-----------------------------------------------------------------------------
fc_init:
        mov     dx, [lpt_base]
        mov     byte [lpt_chip], CHIP_FRIQ
        mov     al, 0FFh
        out     dx, al
        mov     cx, 2                   ; 20 us
        call    delay15
        mov     al, FQ_POWER
        call    fc_cmd
        mov     cx, 33                  ; 500 us
        call    delay15
        call    .probe
        jnc     .found
        mov     byte [lpt_chip], CHIP_FRPW
        call    .probe
        jnc     .found
        ret
.found: call    fc_connect
        mov     ax, (FC_DEVCTL << 8) | 0Eh      ; SRST, nIEN
        call    fc_wr
        mov     cx, 7                   ; 100 us
        call    delay15
        mov     ax, (FC_DEVCTL << 8) | 0Ah      ; nIEN
        call    fc_wr
        mov     cx, 133                 ; 2 ms
        call    delay15
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
.ok:    call    fc_disconnect
        clc
        ret
.gone:  call    fc_disconnect
        stc
        ret

; the test pattern and a task file register.  CF if either does not answer.
.probe: push    es
        push    ds
        pop     es
        call    fc_connect
        mov     di, tf_scratch
        mov     cx, 16
        mov     al, FC_TEST
        call    fc_getr
        mov     si, tf_scratch
        xor     cx, cx
.t:     lodsb
        cmp     al, cl
        jne     .bad
        inc     cl
        cmp     cl, 16
        jb      .t
        mov     ax, ((FC_TF + TF_IREASON) << 8) | 0AAh
        call    fc_wr
        mov     ax, ((FC_TF + TF_IREASON + 1) << 8) | 55h
        call    fc_wr
        mov     ah, FC_TF + TF_IREASON
        call    fc_rd
        cmp     al, 0AAh
        jne     .bad
        call    fc_disconnect
        pop     es
        clc
        ret
.bad:   call    fc_disconnect
        pop     es
        stc
        ret
