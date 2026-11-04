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
