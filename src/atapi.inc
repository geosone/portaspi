;=============================================================================
; atapi.inc - SCSI commands that ATAPI drives (behind EPAT) do not take,
; rewritten for them.  From EUSBASPI's translate_cdb, licensed for PORTASPI
; under the GPL version 2 by its author.
;
;   READ(6), WRITE(6)   -> READ(10), WRITE(10)    (block devices)
;   SEEK(6), REZERO     -> SEEK(10)                (block devices)
;   MODE SENSE(6)       -> MODE SENSE(10), answer converted back
;   MODE SELECT(6)      -> MODE SELECT(10), parameter list converted
;
; License: GNU General Public License version 2 only.
;=============================================================================

XL_NONE         equ     0
XL_MODESENSE    equ     1
XL_MODESELECT   equ     2

SCSI_REZERO_UNIT        equ     01h
SCSI_READ6              equ     08h
SCSI_WRITE6             equ     0Ah
SCSI_SEEK6              equ     0Bh
SCSI_MODE_SELECT6       equ     15h
SCSI_MODE_SENSE6        equ     1Ah
SCSI_SEEK10             equ     2Bh
SCSI_MODE_SELECT10      equ     55h
SCSI_MODE_SENSE10       equ     5Ah
SENSE_ILLEGAL_REQUEST   equ     05h

BOUNCE_SIZE     equ     272             ; 255 bytes of mode data + 8 + slack

s_xlate         db      0
s_blen          dw      0               ; bytes used in the bounce buffer
s_alloc6        dw      0               ; MODE SENSE(6) allocation length
s_msel_bdonly   db      0               ; MODE SELECT(6) without mode pages
s_buf           dd      0               ; the client buffer
bounce          times BOUNCE_SIZE db 0

; Rewrite cdb_buf for an ATAPI drive (SCSI ID x_target).  CF if the
; request cannot be handled.  Sets s_xlate, and s_dir for the mode commands.
translate_cdb:
        mov     byte [s_xlate], XL_NONE
        mov     al, [cdb_buf]
        ; READ(6), WRITE(6), SEEK(6) and REZERO UNIT address blocks only on
        ; disk, WORM, CD and MO devices.  Elsewhere the opcodes mean other
        ; things: on tape 01h is REWIND, and 08h/0Ah carry a transfer
        ; length with a fixed bit, not an LBA.
        movzx   bx, byte [x_target]
        mov     ah, [dev_type+bx]
        and     ah, 1Fh
        cmp     al, SCSI_MODE_SENSE6
        je      .msense
        cmp     al, SCSI_MODE_SELECT6
        je      .mselect
        cmp     ah, 00h                 ; disk
        je      .block
        cmp     ah, 04h                 ; WORM
        je      .block
        cmp     ah, 05h                 ; CD/DVD
        je      .block
        cmp     ah, 07h                 ; optical memory
        jne     .none
.block: cmp     al, SCSI_READ6
        je      .rw6
        cmp     al, SCSI_WRITE6
        je      .rw6
        cmp     al, SCSI_SEEK6
        je      .seek6
        cmp     al, SCSI_REZERO_UNIT
        je      .rezero
.none:  clc
        ret

.rezero:
        ; REZERO UNIT -> SEEK(10) to LBA 0
        mov     bl, [cdb_buf+5]
        xor     eax, eax
        mov     [cdb_buf], eax
        mov     [cdb_buf+4], eax
        mov     byte [cdb_buf], SCSI_SEEK10
        mov     [cdb_buf+9], bl
        clc
        ret

.seek6: ; SEEK(6) -> SEEK(10)
        mov     ah, [cdb_buf+1]
        and     ah, 1Fh                 ; LBA bits 20..16
        mov     dx, [cdb_buf+2]         ; LBA bits 15..0 (big endian)
        mov     bl, [cdb_buf+5]
        mov     byte [cdb_buf], SCSI_SEEK10
        mov     byte [cdb_buf+1], 0
        mov     byte [cdb_buf+2], 0
        mov     [cdb_buf+3], ah
        mov     [cdb_buf+4], dx
        mov     word [cdb_buf+6], 0
        mov     byte [cdb_buf+8], 0
        mov     [cdb_buf+9], bl
        clc
        ret

.rw6:   ; READ(6)/WRITE(6) -> READ(10)/WRITE(10)
        add     al, 20h                 ; 08h->28h, 0Ah->2Ah
        mov     ah, [cdb_buf+1]
        and     ah, 1Fh                 ; LBA bits 20..16
        mov     dx, [cdb_buf+2]         ; LBA bits 15..0 (big endian)
        movzx   cx, byte [cdb_buf+4]    ; length, 0 means 256
        or      cx, cx
        jnz     .len_ok
        mov     cx, 256
.len_ok:
        mov     bl, [cdb_buf+5]
        mov     [cdb_buf], al
        mov     byte [cdb_buf+1], 0
        mov     byte [cdb_buf+2], 0
        mov     [cdb_buf+3], ah
        mov     [cdb_buf+4], dx
        mov     byte [cdb_buf+6], 0
        xchg    cl, ch
        mov     [cdb_buf+7], cx
        mov     [cdb_buf+9], bl
        clc
        ret

.msense:
        ; MODE SENSE(6) -> MODE SENSE(10), answer converted afterwards
        movzx   cx, byte [cdb_buf+4]
        mov     [s_alloc6], cx
        jcxz    .ms_build               ; nothing requested, nothing to fix
        cmp     byte [s_dir], 2
        je      .fail
        mov     byte [s_dir], 1
        mov     byte [s_xlate], XL_MODESENSE
        add     cx, 4
        mov     [s_blen], cx
.ms_build:
        mov     bl, [cdb_buf+5]
        mov     al, [cdb_buf+1]
        and     al, 08h                 ; DBD
        mov     byte [cdb_buf], SCSI_MODE_SENSE10
        mov     [cdb_buf+1], al
        mov     byte [cdb_buf+4], 0
        mov     byte [cdb_buf+5], 0
        mov     byte [cdb_buf+6], 0
        xchg    cl, ch
        mov     [cdb_buf+7], cx
        mov     [cdb_buf+9], bl
        clc
        ret

.mselect:
        ; MODE SELECT(6) -> MODE SELECT(10), parameter list converted now
        movzx   cx, byte [cdb_buf+4]
        jcxz    .sel_build
        cmp     cx, 4
        jb      .fail
        cmp     byte [s_dir], 1
        je      .fail
        mov     byte [s_dir], 2
        mov     byte [s_xlate], XL_MODESELECT
        call    modeselect_prepare
        mov     cx, [s_blen]
.sel_build:
        mov     bl, [cdb_buf+5]
        mov     al, [cdb_buf+1]
        and     al, 11h                 ; PF, SP
        mov     byte [cdb_buf], SCSI_MODE_SELECT10
        mov     [cdb_buf+1], al
        mov     byte [cdb_buf+4], 0
        mov     byte [cdb_buf+5], 0
        mov     byte [cdb_buf+6], 0
        xchg    cl, ch
        mov     [cdb_buf+7], cx
        mov     [cdb_buf+9], bl
        clc
        ret
.fail:  stc
        ret

; ATAPI CD/DVD drives report ANSI version 0 (or 5) in their INQUIRY data.
; ASPICD.SYS only uses the standard SCSI-2 audio commands for drives of
; unknown vendors that claim SCSI-2, so report version 2 for them (as
; EUSBASPI and AHCIASPI do).
inquiry_fixup:
        cmp     byte [cdb_buf], SCSI_INQUIRY
        jne     .out
        test    byte [cdb_buf+1], 1     ; vital product data page
        jnz     .out
        cmp     dword [s_xfer], 3
        jb      .out
        push    es
        push    di
        les     di, [s_buf]
        mov     al, [es:di]
        and     al, 1Fh
        cmp     al, 5                   ; CD/DVD device
        jne     .pop
        mov     al, [es:di+2]
        and     al, 7
        cmp     al, 2
        je      .pop
        mov     byte [es:di+2], 2
.pop:   pop     di
        pop     es
.out:   ret

; build the MODE SELECT(10) parameter list in the bounce buffer
; CX = MODE SELECT(6) parameter list length (>= 4)
modeselect_prepare:
        push    ds
        push    es
        mov     di, bounce
        push    cs
        pop     es
        lds     si, [cs:s_buf]
        movzx   ax, byte [si+3]         ; only a block descriptor, no pages?
        add     ax, 4
        cmp     cx, ax
        setbe   byte [cs:s_msel_bdonly]
        xor     ax, ax
        stosw                           ; mode data length (reserved)
        mov     ax, [si+1]              ; medium type, device specific
        stosw
        xor     ax, ax
        stosw
        stosb
        mov     al, [si+3]              ; block descriptor length
        stosb
        add     si, 4
        sub     cx, 4
        push    cx
        rep     movsb
        pop     cx
        pop     es
        pop     ds
        add     cx, 8
        mov     [s_blen], cx
        ret

; convert a MODE SENSE(10) answer in the bounce buffer to MODE SENSE(6)
; format and copy it to the client buffer
modesense_back:
        push    es
        push    si
        mov     si, bounce
        mov     ax, [si]                ; mode data length (big endian)
        xchg    al, ah
        sub     ax, 3
        jae     .nonneg
        xor     ax, ax
.nonneg:
        cmp     ax, 255
        jbe     .lenok
        mov     ax, 255
.lenok: mov     [si], al
        mov     al, [si+2]              ; medium type
        mov     [si+1], al
        mov     al, [si+3]              ; device specific parameter
        mov     [si+2], al
        mov     al, [si+7]              ; block descriptor length
        mov     [si+3], al
        lea     di, [si+4]              ; move the rest down by 4
        add     si, 8
        mov     cx, [s_alloc6]
        push    cs
        pop     es
        rep     movsb
        ; copy to the client
        movzx   ecx, word [s_alloc6]
        cmp     [s_len], ecx
        jae     .copy
        mov     cx, [s_len]
.copy:  les     di, [s_buf]
        mov     si, bounce
        rep     movsb
        pop     si
        pop     es
        ret

;=============================================================================
; The ATAPI PACKET protocol on the task file behind a bridge chip (EPAT,
; BPCK, ON26), as Linux paride's pf.c and pcd.c have it.  Only SCSI ID 0 exists:
; the master drive behind the bridge.
;=============================================================================

; task file registers
TF_DATA         equ     0
TF_ERROR        equ     1               ; features on writes
TF_IREASON      equ     2
TF_BCLO         equ     4
TF_BCHI         equ     5
TF_DRVHD        equ     6
TF_STATUS       equ     7               ; command on writes

ATA_BSY         equ     80h
ATA_DRQ         equ     08h
ATA_ERR         equ     01h

TF_CHUNK        equ     0F000h          ; at most this per block transfer

tf_bc           dw      0               ; byte count of a DRQ block
tf_scratch      times 16 db 0

; the bridge: connect, disconnect, task file register AH (write AL, or
; read into AL), CX bytes of the drive's data at ES:DI.  DX = base.
br_connect:
        cmp     byte [lpt_chip], CHIP_BPCK
        je      bpck_connect
        cmp     byte [lpt_chip], CHIP_ON26
        je      on26_connect
        cmp     byte [lpt_chip], CHIP_EPIA
        je      epia_connect
        cmp     byte [lpt_chip], CHIP_FRPW
        jae     fc_connect
        jmp     epat_connect

br_disconnect:
        cmp     byte [lpt_chip], CHIP_BPCK
        je      bpck_disconnect
        cmp     byte [lpt_chip], CHIP_ON26
        je      on26_disconnect
        cmp     byte [lpt_chip], CHIP_EPIA
        je      epia_disconnect
        cmp     byte [lpt_chip], CHIP_FRPW
        jae     fc_disconnect
        jmp     epat_disconnect

tf_wr:  push    ax
        cmp     byte [lpt_chip], CHIP_FRPW
        jae     .f
        cmp     byte [lpt_chip], CHIP_ON26
        je      .o
        cmp     byte [lpt_chip], CHIP_EPIA
        je      .e
        cmp     byte [lpt_chip], CHIP_BPCK
        je      .b
        add     ah, 18h                 ; epat.c cont_map[0]
        call    epat_wr
        pop     ax
        ret
.b:     add     ah, 40h                 ; bpck.c cont_map[0]
        call    bpck_wr
        pop     ax
        ret
.o:     call    on26_wr
        pop     ax
        ret
.e:     call    epia_wr                 ; epia.c cont_map[0] is 0
        pop     ax
        ret
.f:     add     ah, FC_TF
        call    fc_wr
        pop     ax
        ret

tf_rd:  push    bx
        mov     bh, ah
        cmp     byte [lpt_chip], CHIP_FRPW
        jae     .f
        cmp     byte [lpt_chip], CHIP_ON26
        je      .o
        cmp     byte [lpt_chip], CHIP_EPIA
        je      .e
        cmp     byte [lpt_chip], CHIP_BPCK
        je      .b
        add     ah, 18h
        call    epat_rd
        jmp     .out
.b:     add     ah, 40h
        call    bpck_rd
        jmp     .out
.o:     call    on26_rd
        jmp     .out
.e:     call    epia_rd
        jmp     .out
.f:     add     ah, FC_TF
        call    fc_rd
.out:   mov     ah, bh
        pop     bx
        ret

blk_get:
        cmp     byte [lpt_chip], CHIP_BPCK
        je      bpck_get
        cmp     byte [lpt_chip], CHIP_ON26
        je      on26_get
        cmp     byte [lpt_chip], CHIP_EPIA
        je      epia_get
        cmp     byte [lpt_chip], CHIP_FRPW
        jae     fc_get
        jmp     epat_get

blk_put:
        cmp     byte [lpt_chip], CHIP_BPCK
        je      bpck_put
        cmp     byte [lpt_chip], CHIP_ON26
        je      on26_put
        cmp     byte [lpt_chip], CHIP_EPIA
        je      epia_put
        cmp     byte [lpt_chip], CHIP_FRPW
        jae     fc_put
        jmp     epat_put

; wait until the drive is not busy.  EBX = timeout in ticks.
; -> AL = ATA status, CF on timeout
atapi_wait:
        push    esi
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.l:     mov     ah, TF_STATUS
        call    tf_rd
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

; SC_RESET_DEV: ATAPI DEVICE RESET
atapi_reset:
        mov     dx, [lpt_base]
        call    br_connect
        mov     ax, (TF_DRVHD << 8) | 0A0h
        call    tf_wr
        mov     ax, (TF_STATUS << 8) | 08h
        call    tf_wr
        mov     ebx, [tmo_ticks]
        call    atapi_wait
        call    br_disconnect
        ret

;-----------------------------------------------------------------------------
; atapi_exec: the ATAPI packet cdb_buf (12 bytes) with the request of
; dev_exec.  The drive's sense key comes back in x_err (error register).
;-----------------------------------------------------------------------------
atapi_exec:
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
.t0:    call    br_connect
        mov     ax, (TF_DRVHD << 8) | 0A0h
        call    tf_wr
        mov     ebx, CMD_TICKS
        call    atapi_wait
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
        mov     ax, TF_ERROR << 8       ; features: PIO
        call    tf_wr
        pop     cx
        mov     ah, TF_BCLO
        mov     al, cl
        call    tf_wr
        mov     ah, TF_BCHI
        mov     al, ch
        call    tf_wr
        mov     ax, (TF_STATUS << 8) | 0A0h     ; PACKET
        call    tf_wr
        mov     ebx, CMD_TICKS
        call    atapi_wait
        jc      .timeout
        test    al, ATA_ERR
        jnz     .check
        test    al, ATA_DRQ
        jz      .phase
        push    cs
        pop     es
        mov     di, cdb_buf
        mov     cx, 12
        call    blk_put

        ; data blocks, each with its DRQ, until the command is done
.drq:   mov     ebx, [tmo_ticks]
        call    atapi_wait
        jc      .timeout
        test    al, ATA_DRQ
        jz      .end
        mov     ah, TF_IREASON
        call    tf_rd
        test    al, 01h                 ; C/D: a packet again?
        jnz     .phase
        mov     bl, 2                   ; out
        test    al, 02h
        jz      .dir
        mov     bl, 1                   ; in
.dir:   mov     ah, TF_BCLO
        call    tf_rd
        mov     cl, al
        mov     ah, TF_BCHI
        call    tf_rd
        mov     ch, al
        or      cx, cx
        jz      .phase
        mov     [tf_bc], cx
.blk:   ; the part that fits the client buffer
        cmp     byte [x_dir], 0
        je      .dirok
        cmp     [x_dir], bl
        jne     .drain
.dirok: mov     eax, [x_len]
        or      eax, eax
        jz      .drain
        movzx   ecx, word [tf_bc]
        cmp     eax, ecx
        jae     .c1
        mov     cx, ax
.c1:    cmp     cx, TF_CHUNK
        jbe     .c2
        mov     cx, TF_CHUNK
.c2:    mov     [x_take], cx
        sub     [tf_bc], cx
        les     di, [x_buf]
        call    .move
        call    xfer_done
        cmp     word [tf_bc], 0
        jne     .blk
        jmp     .drq
.drain: ; beyond the buffer, or the other way: zeros out, bytes in dropped
        mov     byte [x_over], 1
        mov     cx, 16
        cmp     [tf_bc], cx
        jae     .d1
        mov     cx, [tf_bc]
.d1:    sub     [tf_bc], cx
        push    cs
        pop     es
        mov     di, tf_scratch
        push    di
        push    cx
        xor     al, al
        rep     stosb
        pop     cx
        pop     di
        call    .move
        cmp     word [tf_bc], 0
        jne     .drain
        jmp     .drq

.move:  cmp     bl, 2
        je      blk_put
        jmp     blk_get

.end:   test    al, ATA_ERR
        jz      .done
.check: mov     ah, TF_ERROR
        call    tf_rd
        mov     [x_err], al
        mov     byte [x_status], STATUS_CHKCOND
.done:  call    br_disconnect
        mov     al, XR_OK
        jmp     .out
.timeout:
        call    br_disconnect
        mov     al, XR_TIMEOUT
        jmp     .out
.phase: call    br_disconnect
        mov     al, XR_HBA
.out:   pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
