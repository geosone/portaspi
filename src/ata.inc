;=============================================================================
; ata.inc - SCSI commands on an ATA (non-packet) disk behind a bridge
;
; The disk is offered as SCSI ID 0, a direct access device.  The SCSI
; commands of the block drivers are carried out with ATA commands, as in
; AHCIASPI's ata.inc:
;   TEST UNIT READY, REZERO, SEEK,       nothing to do on the drive
;   START STOP, PREVENT ALLOW, RESERVE,
;   RELEASE, MODE SELECT
;   READ / WRITE (6) and (10),           READ / WRITE / VERIFY SECTORS, PIO,
;   WRITE AND VERIFY, VERIFY (10)        LBA or CHS as IDENTIFY tells
;   SYNCHRONIZE CACHE                    FLUSH CACHE (an abort is fine)
;   INQUIRY                              from IDENTIFY DEVICE (vendor "ATA")
;   READ CAPACITY, MODE SENSE (6), (10)  from IDENTIFY: 512 byte blocks
;   REQUEST SENSE                        the sense of the last command
; A failed command leaves sense data mapped from the ATA error register.
; Nothing is retried.
;
; License: GNU General Public License version 2 only.
;=============================================================================

ATA_CMD_READ    equ     20h
ATA_CMD_WRITE   equ     30h
ATA_CMD_VERIFY  equ     40h
ATA_CMD_IDENT   equ     0ECh
ATA_CMD_FLUSH   equ     0E7h

ATAE_NM         equ     02h             ; no media
ATAE_ABRT       equ     04h
ATAE_IDNF       equ     10h
ATAE_MC         equ     20h             ; media changed
ATAE_UNC        equ     40h             ; uncorrectable / write protected

ATA_CHUNK       equ     128             ; sectors per ATA command

; sense data: key | ASC << 8 | ASCQ << 16
SNS_NO_MEDIUM   equ     02h | (3Ah << 8)
SNS_CHANGED     equ     06h | (28h << 8)
SNS_WPROT       equ     07h | (27h << 8)
SNS_UNC         equ     03h | (11h << 8)
SNS_NOT_FOUND   equ     03h | (14h << 8) | (01h << 16)
SNS_RANGE       equ     05h | (21h << 8)
SNS_BAD_OPCODE  equ     05h | (20h << 8)
SNS_BAD_FIELD   equ     05h | (24h << 8)
SNS_ABORTED     equ     0Bh

dev_ata         db      0               ; the drive is an ATA disk
ata_lbam        db      0               ; LBA addressing
ata_rmb         db      0               ; removable
ata_heads       db      0               ; CHS geometry
ata_spt         db      0
ata_cap         dd      0               ; sectors
ata_name        times 20 db ' '         ; INQUIRY product and revision
ata_sns         dd      0               ; sense of the last command
ata_op          db      0
ata_lba         dd      0
ata_left        dd      0
ata_n           dw      0
ata_reply       times 36 db 0

; byte order of EAX reversed (BSWAP is a 486 instruction)
%macro BSWAP_EAX 0
        xchg    al, ah
        rol     eax, 16
        xchg    al, ah
%endmacro

;-----------------------------------------------------------------------------
; ata_exec: the SCSI command cdb_buf with the request of dev_exec
;-----------------------------------------------------------------------------
ata_exec:
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
        mov     al, XR_NOSEL
        cmp     byte [x_target], 0      ; only the master drive
        jne     .out
        mov     al, [cdb_buf]
        cmp     al, SCSI_REQUEST_SENSE
        je      .rsense
        mov     dword [ata_sns], 0      ; a new command clears the sense
        cmp     al, SCSI_INQUIRY
        je      .inquiry
        cmp     al, 25h                 ; READ CAPACITY
        je      .rcap
        cmp     al, 08h                 ; READ (6)
        je      .read6
        cmp     al, 0Ah                 ; WRITE (6)
        je      .write6
        cmp     al, 28h                 ; READ (10)
        je      .read
        cmp     al, 2Ah                 ; WRITE (10)
        je      .write
        cmp     al, 2Eh                 ; WRITE AND VERIFY (10)
        je      .write
        cmp     al, 2Fh                 ; VERIFY (10)
        je      .verify
        cmp     al, 1Ah                 ; MODE SENSE (6)
        je      .msense
        cmp     al, 5Ah                 ; MODE SENSE (10)
        je      .msense
        cmp     al, 35h                 ; SYNCHRONIZE CACHE
        je      .sync
        mov     si, ata_nops
.nop:   cmp     byte [si], 0FFh
        je      .badop
        cmp     al, [si]
        je      .good
        inc     si
        jmp     .nop
.badop: mov     eax, SNS_BAD_OPCODE
        jmp     .sense

.rsense:
        mov     eax, [ata_sns]
        mov     dword [ata_sns], 0
        mov     di, ata_reply
        call    ata_sense_data
        mov     cx, 18
        call    ata_give
        jmp     .good

.inquiry:
        test    byte [cdb_buf+1], 1     ; vital product data
        jnz     .badfield
        push    cs
        pop     es
        mov     di, ata_reply
        mov     eax, 02020000h          ; disk, SCSI-2, format 2
        cmp     byte [ata_rmb], 0
        je      .fixed
        or      ah, 80h
.fixed: stosd
        mov     eax, 31                 ; additional length
        stosd
        mov     eax, 'ATA '
        stosd
        mov     eax, '    '
        stosd
        mov     si, ata_name
        mov     cx, 20
        rep     movsb
        mov     si, ata_reply
        mov     cx, 36
        call    ata_give
        jmp     .good

.rcap:  mov     eax, [ata_cap]
        dec     eax
        BSWAP_EAX
        mov     [ata_reply], eax
        mov     dword [ata_reply+4], 00020000h  ; 512 byte blocks
        mov     si, ata_reply
        mov     cx, 8
        call    ata_give
        jmp     .good

.msense:
        ; header, and a block descriptor unless DBD; no mode pages
        push    cs
        pop     es
        mov     di, ata_reply
        push    di
        xor     eax, eax
        mov     cx, 16 / 4
        rep     stosd
        pop     di
        mov     eax, [ata_cap]
        cmp     eax, 0FFFFFFh
        jbe     .ms_b
        mov     eax, 0FFFFFFh
.ms_b:  BSWAP_EAX                       ; density 0, blocks
        cmp     byte [cdb_buf], 1Ah
        jne     .ms10
        mov     cx, 4
        test    byte [cdb_buf+1], 08h
        jnz     .ms6
        mov     byte [di+3], 8          ; block descriptor length
        mov     [di+4], eax
        mov     dword [di+8], 00020000h
        mov     cx, 12
.ms6:   mov     al, cl
        dec     al
        mov     [di], al                ; mode data length
        mov     si, di
        call    ata_give
        jmp     .good
.ms10:  mov     cx, 8
        test    byte [cdb_buf+1], 08h
        jnz     .ms10b
        mov     byte [di+7], 8
        mov     [di+8], eax
        mov     dword [di+12], 00020000h
        mov     cx, 16
.ms10b: mov     al, cl
        sub     al, 2
        mov     [di+1], al
        mov     si, di
        call    ata_give
        jmp     .good

.read6: mov     byte [ata_op], ATA_CMD_READ
        jmp     .rw6
.write6:
        mov     byte [ata_op], ATA_CMD_WRITE
.rw6:   movzx   eax, byte [cdb_buf+1]
        and     al, 1Fh
        shl     eax, 16
        mov     ah, [cdb_buf+2]
        mov     al, [cdb_buf+3]
        movzx   ecx, byte [cdb_buf+4]
        or      cx, cx
        jnz     .rw
        mov     cx, 256
        jmp     .rw
.read:  mov     byte [ata_op], ATA_CMD_READ
        jmp     .rw10
.write: mov     byte [ata_op], ATA_CMD_WRITE
        jmp     .rw10
.verify:
        test    byte [cdb_buf+1], 02h   ; BYTCHK: compare with host data
        jnz     .badfield
        mov     byte [ata_op], ATA_CMD_VERIFY
.rw10:  mov     eax, [cdb_buf+2]
        BSWAP_EAX
        movzx   ecx, word [cdb_buf+7]
        xchg    cl, ch
.rw:    mov     [ata_lba], eax
        mov     [ata_left], ecx
        mov     edx, [ata_cap]
        sub     edx, eax
        jb      .range
        cmp     edx, ecx
        jb      .range
        mov     dx, [lpt_base]
        cmp     byte [ata_op], ATA_CMD_VERIFY
        je      .rwgo
        mov     eax, [x_len]            ; what the buffer holds
        shr     eax, 9
        cmp     eax, ecx
        jae     .rwgo
        mov     [ata_left], eax
        mov     byte [x_over], 1
.rwgo:  call    ata_rw
        jmp     .result

.sync:  mov     byte [ata_op], ATA_CMD_FLUSH
        call    br_connect
        mov     ax, (TF_STATUS << 8) | ATA_CMD_FLUSH
        call    ata_select
        jc      .sync_t
        call    tf_wr
        mov     ebx, [tmo_ticks]
        call    atapi_wait
.sync_t:
        pushf
        call    br_disconnect
        popf
        mov     al, XR_TIMEOUT
        jc      .out
        jmp     .good                   ; an old drive without it aborts

.result:
        cmp     al, XR_OK
        jne     .out
        or      ah, ah
        jz      .good
        call    ata_error_sense
        jmp     .sense
.range: mov     eax, SNS_RANGE
        jmp     .sense
.badfield:
        mov     eax, SNS_BAD_FIELD
.sense: mov     [ata_sns], eax
        shl     al, 4
        mov     [x_err], al
        mov     byte [x_status], STATUS_CHKCOND
.good:  mov     al, XR_OK
.out:   pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret

; the commands with nothing to do on the drive
ata_nops        db      00h, 01h, 0Bh, 2Bh, 15h, 55h, 16h, 17h, 1Bh, 1Eh, 0FFh

; AH = ATA error register -> EAX = sense
ata_error_sense:
        mov     eax, SNS_NO_MEDIUM
        test    ah, ATAE_NM
        jnz     .out
        mov     eax, SNS_CHANGED
        test    ah, ATAE_MC
        jnz     .out
        mov     eax, SNS_NOT_FOUND
        test    ah, ATAE_IDNF
        jnz     .out
        test    ah, ATAE_UNC
        jz      .abrt
        mov     eax, SNS_UNC
        cmp     byte [ata_op], ATA_CMD_WRITE
        jne     .out
        mov     eax, SNS_WPROT
        jmp     .out
.abrt:  mov     eax, SNS_ABORTED
.out:   ret

; 18 bytes of fixed format sense data for EAX at CS:DI -> SI = the data
ata_sense_data:
        push    es
        push    cs
        pop     es
        push    di
        push    eax
        xor     eax, eax
        mov     cx, 18 / 2
        rep     stosw
        pop     eax
        pop     di
        mov     byte [di], 70h          ; current error, fixed format
        mov     [di+2], al              ; sense key
        mov     byte [di+7], 10         ; additional length
        shr     eax, 8
        mov     [di+12], ax             ; ASC, ASCQ
        mov     si, di
        pop     es
        ret

; copy CX bytes at CS:SI to the request buffer, at most x_len and the
; allocation length the CDB gives
ata_give:
        movzx   ecx, cx
        cmp     ecx, [x_len]
        jbe     .c
        mov     ecx, [x_len]
.c:     mov     [x_xfer], ecx
        les     di, [x_buf]
        rep     movsb
        ret

; select the drive for LBA [ata_lba], with the task file but the command
; set (AX is kept).  CF if it stays busy.
ata_select:
        push    ax
        push    ecx
        push    edx
        mov     eax, [ata_lba]
        cmp     byte [ata_lbam], 0
        je      .chs
        mov     ecx, eax
        mov     ah, 3                   ; sector number: bits 0-7
        mov     al, cl
        call    tf_wr
        mov     ah, TF_BCLO             ; bits 8-15
        mov     al, ch
        call    tf_wr
        shr     ecx, 16
        mov     ah, TF_BCHI             ; bits 16-23
        mov     al, cl
        call    tf_wr
        mov     al, ch                  ; bits 24-27
        and     al, 0Fh
        or      al, 0E0h
        jmp     .dh
.chs:   push    dx
        xor     edx, edx
        movzx   ecx, byte [ata_spt]
        div     ecx                     ; EAX = track, EDX = sector - 1
        inc     dl
        mov     bl, dl
        xor     edx, edx
        movzx   ecx, byte [ata_heads]
        div     ecx                     ; EAX = cylinder, EDX = head
        mov     bh, dl
        pop     dx
        push    ax
        mov     ah, 3
        mov     al, bl
        call    tf_wr
        pop     ax
        push    ax
        mov     ah, TF_BCLO
        call    tf_wr
        pop     ax
        mov     al, ah
        mov     ah, TF_BCHI
        call    tf_wr
        mov     al, bh
        or      al, 0A0h
.dh:    mov     ah, TF_DRVHD
        call    tf_wr
        mov     ebx, CMD_TICKS
        call    atapi_wait
        pop     edx
        pop     ecx
        pop     ax
        ret

; [ata_op] of [ata_left] sectors at [ata_lba], data at x_buf
; -> AL = XR_*, AH = error register
ata_rw:
        call    br_connect
.cmd:   cmp     dword [ata_left], 0
        je      .ok
        mov     ecx, [ata_left]
        cmp     ecx, ATA_CHUNK
        jbe     .n
        mov     ecx, ATA_CHUNK
.n:     mov     [ata_n], cx
        mov     ah, TF_IREASON          ; sector count
        mov     al, cl
        call    tf_wr
        mov     ah, TF_STATUS
        mov     al, [ata_op]
        call    ata_select
        jc      .timeout
        call    tf_wr
.sec:   mov     ebx, [tmo_ticks]
        call    atapi_wait
        jc      .timeout
        test    al, ATA_ERR
        jnz     .error
        cmp     byte [ata_op], ATA_CMD_VERIFY
        je      .next
        test    al, ATA_DRQ
        jz      .phase
        les     di, [x_buf]
        mov     cx, 512
        cmp     byte [ata_op], ATA_CMD_READ
        jne     .put
        call    blk_get
        jmp     .moved
.put:   call    blk_put
.moved: add     dword [x_xfer], 512
        add     word [x_buf+2], 512 / 16
        dec     word [ata_n]
        jnz     .sec
        cmp     byte [ata_op], ATA_CMD_WRITE
        jne     .chunk
        mov     ebx, [tmo_ticks]        ; the last sector written
        call    atapi_wait
        jc      .timeout
        test    al, ATA_ERR
        jnz     .error
        jmp     .chunk
.next:  movzx   ecx, word [ata_n]
        jmp     .adv
.chunk: mov     ecx, ATA_CHUNK
        cmp     ecx, [ata_left]
        jbe     .adv
        mov     ecx, [ata_left]
.adv:   add     [ata_lba], ecx
        sub     [ata_left], ecx
        jmp     .cmd
.ok:    call    br_disconnect
        mov     ax, XR_OK
        ret
.error: mov     ah, TF_ERROR
        call    tf_rd
        mov     ah, al
        or      ah, ah
        jnz     .e
        mov     ah, ATAE_ABRT
.e:     call    br_disconnect
        mov     al, XR_OK
        ret
.timeout:
        call    br_disconnect
        mov     al, XR_TIMEOUT
        ret
.phase: call    br_disconnect
        mov     al, XR_HBA
        ret

;-----------------------------------------------------------------------------
; ata_probe: IDENTIFY DEVICE on the drive (connected, selected) into id_buf
; and its geometry, capacity and name.  CF if it does not answer.
;-----------------------------------------------------------------------------
ata_probe:
        mov     ax, (TF_STATUS << 8) | ATA_CMD_IDENT
        call    tf_wr
        mov     ebx, CMD_TICKS
        call    atapi_wait
        jc      .no
        test    al, ATA_ERR
        jnz     .no
        test    al, ATA_DRQ
        jz      .no
        push    cs
        pop     es
        mov     di, id_buf
        mov     cx, 512
        call    blk_get
        mov     si, id_buf
        mov     al, [si+0]              ; word 0 bit 7: removable
        and     al, 80h
        mov     [ata_rmb], al
        mov     al, [si+3*2]
        mov     [ata_heads], al
        mov     al, [si+6*2]
        mov     [ata_spt], al
        mov     byte [ata_lbam], 0
        test    byte [si+49*2+1], 02h   ; word 49 bit 9: LBA
        jz      .chs
        mov     eax, [si+60*2]
        or      eax, eax
        jz      .chs
        mov     byte [ata_lbam], 1
        jmp     .cap
.chs:   movzx   eax, word [si+1*2]
        movzx   ecx, byte [ata_heads]
        mul     ecx
        movzx   ecx, byte [ata_spt]
        mul     ecx
        or      eax, eax
        jz      .no
.cap:   mov     [ata_cap], eax
        push    cs
        pop     es
        mov     di, ata_name            ; model, words 27..34
        add     si, 27*2
        mov     cx, 8
.m:     lodsw
        xchg    al, ah
        stosw
        loop    .m
        mov     si, id_buf + 23*2       ; firmware, words 23..24
        lodsw
        xchg    al, ah
        stosw
        lodsw
        xchg    al, ah
        stosw
        clc
        ret
.no:    stc
        ret
