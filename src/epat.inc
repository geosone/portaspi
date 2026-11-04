;=============================================================================
; epat.inc - the Shuttle EPAT parallel port to ATAPI bridge (SuperDisk
; LS-120 parallel)
;
; The chip sequences are those of Linux drivers/block/paride/epat.c (Grant
; Guenther), mode 0: register and block reads in nibbles, writes in bytes.
; The ATAPI protocol behind it is atapi_exec (atapi.inc).
;
; License: GNU General Public License version 2 only.
;=============================================================================

; bridge register numbers (epat.c cont_map: task file 18h, control 10h)
EP_DATA         equ     18h
EP_ERROR        equ     19h             ; features on writes
EP_IREASON      equ     1Ah
EP_BCLO         equ     1Ch
EP_BCHI         equ     1Dh
EP_DRVHD        equ     1Eh
EP_STATUS       equ     1Fh             ; command on writes
EP_DEVCTL       equ     16h



;-----------------------------------------------------------------------------
; Bridge access (DX = base)
;-----------------------------------------------------------------------------
; CPP(x): the command packet AL
epat_cpp:
        push    si
        push    ax
        CTR     04h
        mov     si, ep_cpp_seq
.l:     mov     al, [si]
        out     dx, al
        inc     si
        cmp     si, ep_cpp_seq + 7
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

ep_cpp_seq      db      22h, 0AAh, 55h, 00h, 0FFh, 87h, 78h

; epat_write_regr(), mode 0: register AH = AL
epat_wr:
        push    ax
        mov     al, ah
        add     al, 60h
        out     dx, al
        CTR     01h
        pop     ax
        out     dx, al
        CTR     04h
        ret

; epat_read_regr(), mode 0: AL = register AH
epat_rd:
        push    bx
        mov     al, ah
        out     dx, al
        CTR     01h
        CTR     03h
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

; epat_connect()
epat_connect:
        push    ax
        xor     al, al                  ; initialize the chip
        call    epat_cpp
        mov     al, 0E0h                ; connect
        call    epat_cpp
        xor     al, al                  ; idle into SPP
        out     dx, al
        CTR     01h
        CTR     04h
        mov     ax, 0810h
        call    epat_wr
        mov     ax, 0C14h
        call    epat_wr
        mov     ax, 0A38h
        call    epat_wr
        mov     ax, 1210h
        call    epat_wr
        pop     ax
        ret

; epat_disconnect()
epat_disconnect:
        push    ax
        mov     al, 30h
        call    epat_cpp
        CTR     0Ch
        pop     ax
        ret

; epat_read_block(), mode 0: CX bytes to ES:DI (CX > 0)
epat_get:
        push    bx
        mov     al, 7
        out     dx, al
        CTR     01h
        CTR     03h
        mov     al, 0FFh
        out     dx, al
        xor     bl, bl                  ; ph
.l:     cmp     cx, 1
        jne     .nl
        mov     al, 0FDh                ; the last byte
        out     dx, al
.nl:    add     dx, 2
        mov     al, 6
        or      al, bl
        out     dx, al
        dec     dx
        in      al, dx
        mov     bh, al
        test    al, 08h
        jnz     .one                    ; both nibbles in one read
        inc     dx
        mov     al, 4
        or      al, bl
        out     dx, al
        dec     dx
        in      al, dx
.one:   dec     dx
        and     al, 0F0h
        shr     bh, 4
        or      al, bh
        stosb
        xor     bl, 1
        loop    .l
        xor     al, al
        out     dx, al
        CTR     04h
        pop     bx
        ret

; epat_write_block(), modes 0 to 2: CX bytes from ES:DI (CX > 0)
epat_put:
        push    bx
        mov     al, 67h
        out     dx, al
        CTR     01h
        CTR     05h
        xor     bl, bl
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

;-----------------------------------------------------------------------------
; epat_init: an EPAT with a drive on lpt_base?  As epat_test_proto(): the
; drive's task file must keep what is written to it.  CF if not.
;-----------------------------------------------------------------------------
epat_init:
        mov     dx, [lpt_base]
        call    epat_connect
        mov     ax, (EP_DRVHD << 8) | 0A0h
        call    epat_wr
        xor     cx, cx
        mov     bl, 0
.t:     mov     ah, EP_IREASON          ; sector count
        mov     al, bl
        xor     al, 0AAh
        call    epat_wr
        mov     ah, 1Bh                 ; sector number
        mov     al, bl
        xor     al, 55h
        call    epat_wr
        mov     ah, EP_IREASON
        call    epat_rd
        xor     al, 0AAh
        cmp     al, bl
        je      .same
        inc     cx
.same:  add     bl, 13
        cmp     bl, 13 * 19
        jb      .t
        call    epat_disconnect
        cmp     cx, 1
        cmc                             ; CF if any value was lost
        ret

