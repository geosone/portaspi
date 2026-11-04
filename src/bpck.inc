;=============================================================================
; bpck.inc - the Micro Solutions BackPack parallel port to ATAPI bridge
; (BackPack CD-ROM)
;
; The sequences are those of Linux drivers/block/paride/bpck.c (Grant
; Guenther), modes 0 (4-bit: reads in nibbles on the status lines) and
; 1 (8-bit: reads on the data lines).  The ATAPI protocol behind it is
; atapi_exec (atapi.inc).  The bridges chain; the unit is found at init.
;
; Most of bpck.c works on the control register through a copy of it
; (t2: toggle bits, o2/e2: set/clear STROBE); bk_pc is that copy.
;
; License: GNU General Public License version 2 only.
;=============================================================================

bk_pc           db      0               ; the control register as written
bk_unit         db      0               ; chain address

; w2(AL)
bk_w2:  mov     [bk_pc], al
        add     dx, 2
        out     dx, al
        sub     dx, 2
        ret

; t2(AL): toggle bits of the control register
bk_t2:  xor     al, [bk_pc]
        jmp     bk_w2

%macro T2 1
        mov     al, %1
        call    bk_t2
%endmacro

; o2(): STROBE on
bk_o2:  mov     al, [bk_pc]
        or      al, 1
        jmp     bk_w2

; e2(): STROBE off
bk_e2:  mov     al, [bk_pc]
        and     al, 0FEh
        jmp     bk_w2

; r1(): AL = status
bk_r1:  inc     dx
        in      al, dx
        dec     dx
        ret

; j44(l, h) of bpck.c: AL = l, AH = h -> AL
bk_j44: push    bx
        mov     bl, al
        shr     bl, 3
        and     bl, 07h
        shr     al, 4
        and     al, 08h
        or      bl, al
        mov     al, ah
        shl     al, 1
        and     al, 70h
        or      bl, al
        and     ah, 80h
        or      bl, ah
        mov     al, bl
        pop     bx
        ret

; bpck_write_regr(), modes 0 and 1: register AH = AL
bpck_wr:
        push    ax
        mov     al, ah
        out     dx, al
        T2      2
        pop     ax
        out     dx, al
        call    bk_o2
        T2      4
        T2      1
        ret

; bpck_read_regr(): AL = register AH
bpck_rd:
        push    bx
        mov     al, ah
        and     al, 0Fh
        out     dx, al
        mov     al, ah
        out     dx, al
        T2      2
        cmp     byte [lpt_mode], MODE_PS2
        je      .ps2
        T2      4                       ; mode 0
        call    bk_r1
        mov     bl, al
        T2      4
        call    bk_r1
        mov     ah, al
        mov     al, bl
        call    bk_j44
        pop     bx
        ret
.ps2:   call    bk_e2                   ; mode 1
        T2      20h
        T2      4
        in      al, dx
        mov     bl, al
        T2      1
        T2      20h
        mov     al, bl
        pop     bx
        ret

; bpck_connect(), with the setup for a CD-ROM (pcd)
bpck_connect:
        push    ax
        mov     al, 0FFh
        sub     al, [bk_unit]
        out     dx, al
        mov     al, 4
        call    bk_w2
        mov     al, [bk_unit]
        out     dx, al
        T2      8
        T2      8
        T2      8
        T2      2
        T2      2
        T2      8
        mov     ax, 0400h               ; mode 0
        cmp     byte [lpt_mode], MODE_PS2
        jne     .m
        mov     al, 10h                 ; mode 1
.m:     call    bpck_wr
        mov     ax, 0508h
        call    bpck_wr
        mov     ax, 4610h               ; "fiddle with ESS logic ???"
        call    bpck_wr
        mov     ax, 4C38h
        call    bpck_wr
        mov     ax, 4D88h
        call    bpck_wr
        mov     ax, 46A0h
        call    bpck_wr
        mov     ax, 4100h
        call    bpck_wr
        mov     ax, 4E08h
        call    bpck_wr
        pop     ax
        ret

; bpck_disconnect()
bpck_disconnect:
        push    ax
        xor     al, al
        out     dx, al
        T2      2
        mov     al, 4Ch
        call    bk_w2
        pop     ax
        ret

; bpck_read_block(): CX bytes to ES:DI
bpck_get:
        push    bx
        cmp     byte [lpt_mode], MODE_PS2
        je      .ps2
        mov     ax, 0440h               ; mode 0
        call    bpck_wr
        mov     al, 40h
        out     dx, al
        T2      2
.n:     T2      4
        call    bk_r1
        mov     bl, al
        T2      4
        call    bk_r1
        mov     ah, al
        mov     al, bl
        call    bk_j44
        stosb
        loop    .n
        mov     ax, 0400h
        call    bpck_wr
        pop     bx
        ret
.ps2:   mov     ax, 0450h               ; mode 1
        call    bpck_wr
        mov     al, 40h
        out     dx, al
        T2      2
        T2      20h
.b:     T2      4
        in      al, dx
        stosb
        loop    .b
        T2      1
        T2      20h
        mov     ax, 0410h
        call    bpck_wr
        pop     bx
        ret

; bpck_write_block(): CX bytes from ES:DI
bpck_put:
        mov     ax, 0440h               ; mode 0
        cmp     byte [lpt_mode], MODE_PS2
        jne     .m
        mov     al, 50h                 ; mode 1
.m:     call    bpck_wr
        mov     al, 40h
        out     dx, al
        T2      2
        T2      1
.l:     mov     al, [es:di]
        inc     di
        out     dx, al
        T2      4
        loop    .l
        mov     ax, 0400h
        cmp     byte [lpt_mode], MODE_PS2
        jne     .e
        mov     al, 10h
.e:     jmp     bpck_wr

;-----------------------------------------------------------------------------
; bpck_init: a BackPack on lpt_base?  As bpck_probe_unit() for units 0 to
; 7, but a unit only counts when its two address readings are
; complements (AUTOFD set: the unit, clear: its complement, in status bits
; 5-3).  Then the drive's task file must keep what is written to it, as
; for the EPAT.  CF if there is none.
;-----------------------------------------------------------------------------
bpck_init:
        mov     dx, [lpt_base]
        xor     bl, bl
.unit:  mov     al, 4
        call    bk_w2
        mov     al, 0Eh
        call    bk_w2
        mov     al, 0FFh
        sub     al, bl
        out     dx, al
        mov     al, 4
        call    bk_w2
        mov     al, bl
        out     dx, al
        T2      8
        T2      8
        T2      8
        T2      2                       ; AUTOFD set: the unit
        call    bk_r1
        mov     bh, al
        T2      2                       ; AUTOFD clear: its complement
        call    bk_r1
        xor     al, bh
        and     al, 38h
        cmp     al, 38h
        jne     .next
        shr     bh, 3
        and     bh, 07h
        mov     al, bl
        and     al, 07h
        cmp     al, bh
        je      .found
.next:  T2      8                       ; as bpck_probe_unit() leaves it
        xor     al, al
        out     dx, al
        T2      2
        mov     al, 4Ch
        call    bk_w2
        inc     bl
        cmp     bl, 8
        jb      .unit
        stc
        ret
.found: T2      8
        xor     al, al
        out     dx, al
        T2      2
        mov     al, 4Ch
        call    bk_w2
        mov     [bk_unit], bl
        call    bpck_connect
        mov     ax, (TF_DRVHD << 8) | 0A0h
        call    tf_wr
        xor     cx, cx
        xor     bl, bl
.t:     mov     ah, TF_IREASON          ; sector count
        mov     al, bl
        xor     al, 0AAh
        call    tf_wr
        mov     ah, 3                   ; sector number
        mov     al, bl
        xor     al, 55h
        call    tf_wr
        mov     ah, TF_IREASON
        call    tf_rd
        xor     al, 0AAh
        cmp     al, bl
        je      .same
        inc     cx
.same:  add     bl, 13
        cmp     bl, 13 * 19
        jb      .t
        call    bpck_disconnect
        cmp     cx, 1
        cmc
        ret
