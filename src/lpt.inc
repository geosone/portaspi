;=============================================================================
; lpt.inc - what the parallel port chips share: the request, timing, EPP
; helpers and the data buffer
;
; License: GNU General Public License version 2 only.
;=============================================================================

CHIP_PPA        equ     0               ; Iomega PPA3 (ZIP 100), ppa.inc
CHIP_IMM        equ     1               ; Iomega IMM (ZIP Plus, ZIP 250), imm.inc
CHIP_EPAT       equ     2               ; Shuttle EPAT to ATAPI, epat.inc
CHIP_BPCK       equ     3               ; Micro Solutions BackPack to ATAPI, bpck.inc
CHIP_ON26       equ     4               ; OnSpec 90c26 to ATA or ATAPI, on26.inc
CHIP_EPIA       equ     5               ; Shuttle EPIA to ATA or ATAPI, epia.inc
CHIP_FRPW       equ     6               ; Freecom Power to ATA or ATAPI, fc.inc
CHIP_FRIQ       equ     7               ; Freecom IQ to ATA or ATAPI, fc.inc

BURST           equ     512             ; ppa.c PPA_BURST_SIZE, imm.c IMM_BURST_SIZE
SEL_TICKS       equ     2               ; selection: at least one tick
CMD_TICKS       equ     19              ; command bytes: about a second
MSG_TICKS       equ     2               ; optional message byte

lpt_base        dw      0
lpt_mode        db      MODE_NIBBLE
lpt_ecr         db      0               ; ECP port (ECR at base + 402h)
lpt_chip        db      CHIP_PPA

; request for dev_exec
x_target        db      0               ; SCSI ID
cdb_len         db      0
cdb_buf         times 16 db 0
x_buf           dd      0               ; data buffer (far pointer)
x_len           dd      0               ; bytes left in the buffer
x_dir           db      0               ; 0 any, 1 in, 2 out
; result
x_xfer          dd      0               ; bytes moved to or from the buffer
x_over          db      0               ; the drive moved more, or the other way
x_status        db      0               ; SCSI status byte
x_msg           db      0
x_err           db      0               ; ATAPI: the error register
x_bulk          db      0
x_dummy         dw      0               ; overrun bytes
x_take          dw      0               ; buffer bytes of this transfer
x_pad           dw      0               ; IMM: an odd last byte and a pad

; control register through DX = base
%macro CTR 1
        mov     al, %1
        add     dx, 2
        out     dx, al
        sub     dx, 2
%endmacro

;-----------------------------------------------------------------------------
; Timing
;-----------------------------------------------------------------------------
get_ticks:                              ; -> EAX = BIOS tick counter
        push    ds
        push    word 40h
        pop     ds
        mov     eax, [6Ch]
        pop     ds
        ret

; at least 15 us (two toggles; the IMM steps need 1 to 10 us)
delay1: push    cx
        mov     cx, 2
        call    delay15
        pop     cx
        ret

; wait CX periods of the 15 us refresh toggle (port 61h bit 4)
delay15:
        jcxz    .out
        push    ax
        push    bx
        push    cx
        in      al, 61h
        and     al, 10h
        mov     ah, al
.next:  mov     bx, 1000                ; at most about 1 ms per period
.w:     in      al, 61h
        and     al, 10h
        cmp     al, ah
        jne     .t
        dec     bx
        jnz     .w
.t:     mov     ah, al
        loop    .next
        pop     cx
        pop     bx
        pop     ax
.out:   ret

; Wait until (status & AH) == CH.  EBX = timeout in ticks, DX = base.
; -> AL = status, CF on timeout.  Interrupts are enabled meanwhile.
wait_bits:
        push    esi
        mov     [w_mask], ah
        mov     [w_want], ch
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.poll:  inc     dx
        in      al, dx
        dec     dx
        mov     [w_stat], al
        and     al, [w_mask]
        cmp     al, [w_want]
        je      .ok
        call    get_ticks
        sub     eax, esi
        jns     .chk
        call    get_ticks               ; midnight wrap: restart
        mov     esi, eax
        jmp     .poll
.chk:   cmp     eax, ebx
        jbe     .poll
        popf
        mov     al, [w_stat]
        pop     esi
        stc
        ret
.ok:    popf
        mov     al, [w_stat]
        pop     esi
        clc
        ret

w_mask          db      0
w_want          db      0
w_stat          db      0

; ppa_wait(): wait for BSY and REQ (bits 7 and 6).  EBX = timeout in ticks.
; -> AL = status & F0h, CF on timeout
epp_reset:
        inc     dx
        in      al, dx
        out     dx, al
        and     al, 0FEh
        out     dx, al
        dec     dx
        ret

; EPP data cycles, the same for PPA and IMM (ppa_out()/imm_out() and
; ppa_in()/imm_in() in EPP mode): CX bytes at ES:DI.  CF on a timeout.
epp_put:
        call    epp_reset
        CTR     04h
        add     dx, 4
.e:     mov     al, [es:di]
        inc     di
        out     dx, al
        loop    .e
        sub     dx, 4
        CTR     0Ch
        jmp     epp_done

epp_get:
        call    epp_reset
        CTR     24h
        add     dx, 4
        rep     insb
        sub     dx, 4
        CTR     2Ch
        jmp     epp_done

; the end of an EPP transfer: CF if the port timed out (status bit 0);
; the control register is written again with what it has
epp_done:
        inc     dx
        in      al, dx
        dec     dx
        mov     ah, al
        add     dx, 2
        in      al, dx
        out     dx, al
        sub     dx, 2
        test    ah, 1
        jz      .ok
        stc
        ret
.ok:    clc
        ret

norm_buf:
        push    ax
        push    bx
        mov     ax, di
        shr     ax, 4
        mov     bx, es
        add     ax, bx
        and     di, 0Fh
        mov     [x_buf], di
        mov     [x_buf+2], ax
        pop     bx
        pop     ax
        ret


;-----------------------------------------------------------------------------
; The chip of the port
;-----------------------------------------------------------------------------
dev_exec:
        push    ax
        mov     al, [x_target]
        cmp     al, [qt_id]             ; the floppy tape
        pop     ax
        je      qic_exec
        cmp     byte [dev_fdd], 0       ; the BackPack's floppy drive
        jne     fdc_exec
        cmp     byte [dev_mcd], 0       ; the first BackPack CD-ROMs' Mitsumi
        jne     mcd_exec
        cmp     byte [dev_ata], 0       ; an ATA disk behind a bridge
        jne     ata_exec
        cmp     byte [lpt_chip], CHIP_EPAT
        jae     atapi_exec
        cmp     byte [lpt_chip], CHIP_IMM
        je      imm_exec
        jmp     ppa_exec

dev_bus_reset:
        cmp     byte [dev_fdd], 0       ; nothing to reset: no bus
        jne     .out
        cmp     byte [qt_bp], 0         ; the Ditto
        jne     .out
        cmp     byte [dev_ata], 0
        je      .bus
.out:   ret
.bus:   cmp     byte [lpt_chip], CHIP_EPAT
        jae     atapi_reset
        cmp     byte [lpt_chip], CHIP_IMM
        je      imm_bus_reset
        jmp     ppa_bus_reset

; x_bulk for commands that move whole blocks: 512 byte bursts
set_bulk:
        mov     byte [x_bulk], 0
        mov     al, [cdb_buf]
        cmp     al, 08h                 ; READ(6)
        je      .bulk
        cmp     al, 0Ah                 ; WRITE(6)
        je      .bulk
        cmp     al, 28h                 ; READ(10)
        je      .bulk
        cmp     al, 2Ah                 ; WRITE(10)
        je      .bulk
        cmp     al, 2Eh                 ; WRITE AND VERIFY(10)
        jne     .out
.bulk:  mov     byte [x_bulk], 1
.out:   ret

; Where the next transfer of the data phase goes.  AH = the direction the
; drive asks for (1 in, 2 out), CX = its unit (1, or 2 for IMM byte pairs).
; -> ES:DI and CX bytes on the port, x_take bytes of the client buffer.
; Data beyond the buffer, or in the other direction, is drained: zeros out,
; bytes in thrown away, and x_over set.
xfer_setup:
        cmp     byte [x_bulk], 0
        je      .unit
        cmp     dword [x_len], BURST
        jb      .unit
        mov     cx, BURST
.unit:  cmp     byte [x_dir], 0
        je      .dirok
        cmp     [x_dir], ah
        jne     .over
.dirok: cmp     dword [x_len], 0
        je      .over
        mov     [x_take], cx
        les     di, [x_buf]
        movzx   eax, cx                 ; (AH is not needed any more)
        cmp     [x_len], eax
        jae     .out
        ; one byte left for a byte pair: it and a zero
        mov     word [x_take], 1
        mov     al, [es:di]
        xor     ah, ah
        mov     [x_pad], ax
        push    cs
        pop     es
        mov     di, x_pad
.out:   ret
.over:  mov     byte [x_over], 1
        mov     word [x_take], 0
        mov     word [x_dummy], 0
        push    cs
        pop     es
        mov     di, x_dummy
        ret

; after the transfer: account x_take bytes of the client buffer
xfer_done:
        push    eax
        movzx   eax, word [x_take]
        or      ax, ax
        jz      .out
        sub     [x_len], eax
        add     [x_xfer], eax
        les     di, [x_buf]
        add     di, ax
        call    norm_buf
.out:   pop     eax
        ret
