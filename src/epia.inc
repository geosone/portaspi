;=============================================================================
; epia.inc - the Shuttle EPIA parallel port to IDE bridge (Shuttle's parallel
; port hard disk cases, SyQuest's parallel port drives)
;
; The chip sequences are those of Linux drivers/block/paride/epia.c (Grant
; Guenther): mode 0 (register and block reads in nibbles) and mode 2 (8-bit
; reads, the port reversed), writes in bytes.  The probe is the link test of
; Shuttle's EPIAHD.SYS: command 80h hands the data lines back.
;
; License: GNU General Public License version 2 only.
;=============================================================================

EI_DEVCTL       equ     86h             ; epia.c cont_map[1] + 6
EI_ECHO         equ     80h             ; the link test command

;-----------------------------------------------------------------------------
; Bridge access (DX = base)
;-----------------------------------------------------------------------------
; epia_write_regr(), modes 0 to 2: register AH = AL
epia_wr:
        push    ax
        mov     al, ah
        xor     al, 19h
        out     dx, al
        CTR     01h
        pop     ax
        out     dx, al
        CTR     03h
        CTR     04h
        ret

; epia_read_regr(), mode 0 or 2: AL = register AH
epia_rd:
        push    bx
        cmp     byte [lpt_mode], MODE_PS2
        je      .ps2
        mov     al, ah
        xor     al, 39h
        out     dx, al
        CTR     01h
        CTR     03h
        mov     al, ah
        xor     al, 39h
        out     dx, al
        inc     dx
        in      al, dx                  ; low nibble in the high half
        mov     bl, al
        inc     dx
        mov     al, 01h
        out     dx, al
        dec     dx
        in      al, dx                  ; high nibble
        dec     dx
        and     al, 0F0h
        shr     bl, 4
        or      al, bl
        jmp     .out
.ps2:   mov     al, ah
        xor     al, 29h
        out     dx, al
        CTR     01h
        CTR     21h
        CTR     23h
        in      al, dx
.out:   mov     bl, al
        CTR     04h
        mov     al, bl
        pop     bx
        ret

; epia_connect()
epia_connect:
        push    ax
        push    si
        CTR     04h
        mov     si, ei_wake
.l:     mov     al, [si]
        out     dx, al
        inc     si
        cmp     si, ei_wake + 6
        jb      .l
        CTR     01h
        CTR     04h
        mov     ax, (EI_DEVCTL << 8) | 08h
        call    epia_wr
        pop     si
        pop     ax
        ret

ei_wake         db      0A0h, 50h, 0C0h, 30h, 0A0h, 00h

; epia_disconnect()
epia_disconnect:
        push    ax
        xor     al, al
        out     dx, al
        CTR     01h
        CTR     04h
        CTR     0Ch
        pop     ax
        ret

; epia_read_block(), mode 0 or 2: CX bytes to ES:DI (CX > 0)
epia_get:
        push    bx
        mov     bl, 1                   ; ph
        cmp     byte [lpt_mode], MODE_PS2
        je      .ps2
        mov     al, 81h
        out     dx, al
        CTR     01h
        CTR     03h
        mov     al, 0C1h
        out     dx, al
.n:     add     dx, 2
        mov     al, 2
        or      al, bl
        out     dx, al
        dec     dx
        in      al, dx                  ; low nibble
        mov     bh, al
        inc     dx
        mov     al, 4
        or      al, bl
        out     dx, al
        dec     dx
        in      al, dx                  ; high nibble
        dec     dx
        and     al, 0F0h
        shr     bh, 4
        or      al, bh
        stosb
        xor     bl, 1
        loop    .n
        xor     al, al
        out     dx, al
        CTR     04h
        pop     bx
        ret
.ps2:   mov     al, 89h
        out     dx, al
        CTR     01h
        CTR     23h
        CTR     21h
.b:     add     dx, 2
        mov     al, 24h
        or      al, bl
        out     dx, al
        sub     dx, 2
        in      al, dx
        stosb
        xor     bl, 1
        loop    .b
        CTR     06h
        CTR     04h
        pop     bx
        ret

; epia_write_block(), modes 0 to 2: CX bytes from ES:DI (CX > 0)
epia_put:
        push    bx
        mov     al, 0A1h
        out     dx, al
        CTR     01h
        CTR     03h
        CTR     01h
        CTR     05h
        xor     bl, bl                  ; ph
.l:     mov     al, [es:di]
        inc     di
        out     dx, al
        add     dx, 2
        mov     al, 4
        or      al, bl
        out     dx, al
        sub     dx, 2
        xor     bl, 1
        loop    .l
        CTR     07h
        CTR     04h
        pop     bx
        ret

; the link test: AL out, AL back in nibbles
epia_echo:
        push    bx
        mov     bh, al
        mov     al, EI_ECHO
        out     dx, al
        CTR     01h
        mov     al, bh
        out     dx, al
        CTR     03h
        inc     dx
        in      al, dx                  ; low nibble
        mov     bl, al
        inc     dx
        mov     al, 01h
        out     dx, al
        dec     dx
        in      al, dx                  ; high nibble
        dec     dx
        and     al, 0F0h
        shr     bl, 4
        or      al, bl
        mov     bl, al
        CTR     04h
        mov     al, bl
        pop     bx
        ret

;-----------------------------------------------------------------------------
; epia_init: an EPIA with a drive on lpt_base?  The link test of EPIAHD.SYS
; (80h and ECh, each rotated through all eight positions, must come back),
; then a soft reset through the device control register and the signature
; tell an ATAPI drive (14h EBh) from an ATA disk.  CF if none.
; -> dev_ata: 1 for an ATA disk, 0 for an ATAPI drive
;-----------------------------------------------------------------------------
epia_init:
        mov     dx, [lpt_base]
        call    epia_connect
        mov     bl, 80h
        call    .test
        jc      .gone
        mov     bl, 0ECh
        call    .test
        jc      .gone
        mov     byte [lpt_chip], CHIP_EPIA
        mov     ax, (EI_DEVCTL << 8) | 0Eh      ; SRST, nIEN
        call    epia_wr
        mov     cx, 7                   ; 100 us
        call    delay15
        mov     ax, (EI_DEVCTL << 8) | 0Ah      ; nIEN
        call    epia_wr
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
.ok:    call    epia_disconnect
        clc
        ret
.gone:  call    epia_disconnect
        stc
        ret

; BL rotated through eight positions, each echoed back.  CF if one is not.
.test:  mov     cx, 8
.t:     mov     al, bl
        call    epia_echo
        cmp     al, bl
        jne     .bad
        ror     bl, 1
        loop    .t
        clc
        ret
.bad:   stc
        ret
