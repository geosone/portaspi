;=============================================================================
; epat.inc - the Shuttle EPAT parallel port to ATAPI bridge (SuperDisk
; LS-120 parallel)
;
; The chip sequences are those of Linux drivers/block/paride/epat.c (Grant
; Guenther), mode 0: register and block reads in nibbles, writes in bytes.
; The ATAPI protocol on the task file behind it follows paride's pf.c.
; Only SCSI ID 0 exists: the master drive behind the bridge.
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

ATA_BSY         equ     80h
ATA_DRQ         equ     08h
ATA_ERR         equ     01h

EP_CHUNK        equ     0F000h          ; at most this per block transfer

ep_bc           dw      0               ; byte count of a DRQ block
ep_scratch      times 16 db 0

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

; wait until the drive is not busy.  EBX = timeout in ticks.
; -> AL = ATA status, CF on timeout
epat_wait:
        push    esi
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.l:     mov     ah, EP_STATUS
        call    epat_rd
        test    al, ATA_BSY
        jz      .ok
        push    ax
        call    get_ticks
        sub     eax, esi
        jns     .chk
        call    get_ticks
        mov     esi, eax
        pop     ax
        jmp     .l
.chk:   cmp     eax, ebx
        pop     ax
        jbe     .l
        popf
        pop     esi
        stc
        ret
.ok:    popf
        pop     esi
        clc
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

; SC_RESET_DEV: ATAPI DEVICE RESET
epat_reset:
        mov     dx, [lpt_base]
        call    epat_connect
        mov     ax, (EP_DRVHD << 8) | 0A0h
        call    epat_wr
        mov     ax, (EP_STATUS << 8) | 08h
        call    epat_wr
        mov     ebx, [tmo_ticks]
        call    epat_wait
        call    epat_disconnect
        ret

;-----------------------------------------------------------------------------
; epat_exec: the ATAPI packet cdb_buf (12 bytes) with the request of
; dev_exec.  The drive's sense key comes back in x_err (error register).
;-----------------------------------------------------------------------------
epat_exec:
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        xor     eax, eax
        mov     [x_xfer], eax
        mov     [x_over], al
        mov     [x_status], al
        mov     [x_err], al
        les     di, [x_buf]
        call    norm_buf
        mov     dx, [lpt_base]
        cmp     byte [x_target], 0      ; only the master drive
        je      .t0
        mov     al, XR_NOSEL
        jmp     .out
.t0:    call    epat_connect
        mov     ax, (EP_DRVHD << 8) | 0A0h
        call    epat_wr
        mov     ebx, CMD_TICKS
        call    epat_wait
        jc      .timeout
        ; the byte count limit: the buffer, at most FFFEh, never 0
        mov     eax, [x_len]
        cmp     eax, 0FFFEh
        jbe     .bc
        mov     ax, 0FFFEh
.bc:    or      ax, ax
        jnz     .bc2
        mov     ax, 2
.bc2:   push    ax
        mov     ax, EP_ERROR << 8       ; features: PIO
        call    epat_wr
        pop     cx
        mov     ah, EP_BCLO
        mov     al, cl
        call    epat_wr
        mov     ah, EP_BCHI
        mov     al, ch
        call    epat_wr
        mov     ax, (EP_STATUS << 8) | 0A0h     ; PACKET
        call    epat_wr
        mov     ebx, CMD_TICKS
        call    epat_wait
        jc      .timeout
        test    al, ATA_ERR
        jnz     .check
        test    al, ATA_DRQ
        jz      .phase
        push    cs
        pop     es
        mov     di, cdb_buf
        mov     cx, 12
        call    epat_put

        ; data blocks, each with its DRQ, until the command is done
.drq:   mov     ebx, [tmo_ticks]
        call    epat_wait
        jc      .timeout
        test    al, ATA_DRQ
        jz      .end
        mov     ah, EP_IREASON
        call    epat_rd
        test    al, 01h                 ; C/D: a packet again?
        jnz     .phase
        mov     bl, 2                   ; out
        test    al, 02h
        jz      .dir
        mov     bl, 1                   ; in
.dir:   mov     ah, EP_BCLO
        call    epat_rd
        mov     cl, al
        mov     ah, EP_BCHI
        call    epat_rd
        mov     ch, al
        or      cx, cx
        jz      .phase
        mov     [ep_bc], cx
.blk:   ; the part that fits the client buffer
        cmp     byte [x_dir], 0
        je      .dirok
        cmp     [x_dir], bl
        jne     .drain
.dirok: mov     eax, [x_len]
        or      eax, eax
        jz      .drain
        movzx   ecx, word [ep_bc]
        cmp     eax, ecx
        jae     .c1
        mov     cx, ax
.c1:    cmp     cx, EP_CHUNK
        jbe     .c2
        mov     cx, EP_CHUNK
.c2:    mov     [x_take], cx
        sub     [ep_bc], cx
        les     di, [x_buf]
        call    .move
        call    xfer_done
        cmp     word [ep_bc], 0
        jne     .blk
        jmp     .drq
.drain: ; beyond the buffer, or the other way: zeros out, bytes in dropped
        mov     byte [x_over], 1
        mov     cx, 16
        cmp     [ep_bc], cx
        jae     .d1
        mov     cx, [ep_bc]
.d1:    sub     [ep_bc], cx
        push    cs
        pop     es
        mov     di, ep_scratch
        push    di
        push    cx
        xor     al, al
        rep     stosb
        pop     cx
        pop     di
        call    .move
        cmp     word [ep_bc], 0
        jne     .drain
        jmp     .drq

.move:  cmp     bl, 2
        je      epat_put
        jmp     epat_get

.end:   test    al, ATA_ERR
        jz      .done
.check: mov     ah, EP_ERROR
        call    epat_rd
        mov     [x_err], al
        mov     byte [x_status], STATUS_CHKCOND
.done:  call    epat_disconnect
        mov     al, XR_OK
        jmp     .out
.timeout:
        call    epat_disconnect
        mov     al, XR_TIMEOUT
        jmp     .out
.phase: call    epat_disconnect
        mov     al, XR_HBA
.out:   pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
