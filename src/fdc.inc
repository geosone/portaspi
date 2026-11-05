;=============================================================================
; fdc.inc - SCSI commands on the floppy drive of a Micro Solutions BackPack
; floppy pod: a 765 floppy controller behind the BackPack link
;
; The pod has the 765's registers at 40h-47h, a 128 KB buffer the host
; reaches through register 80h at the address in register 28h, and a DMA
; engine that moves the controller's data between the drive and that
; buffer: register 2Ch is where it starts, 30h the length less one (both
; three bytes, low first), register 04h sets the direction and starts it.
; Register 0Bh bit 7 is the controller's interrupt line.  This is the way
; Micro Solutions' BACKPACK.SYS uses it.
;
; The drive is offered as SCSI ID 0, a removable direct access device:
;   TEST UNIT READY                      the change line, then the medium
;   INQUIRY                              "MICROSOL BACKPACK FLOPPY"
;   READ CAPACITY, MODE SENSE (6), (10)  from the medium (READ ID at the
;                                        data rates the drive takes)
;   READ / WRITE (6) and (10),           READ DATA / WRITE DATA, a track
;   WRITE AND VERIFY, VERIFY (10)        side at a time through the buffer
;   REZERO, SEEK, START STOP, PREVENT    nothing to do
;   ALLOW, RESERVE, RELEASE, MODE SELECT
;   REQUEST SENSE                        the sense of the last command
; A changed disk is reported once as UNIT ATTENTION.  The motor goes off
; two seconds after the last command (INT 1Ch).
;
; License: GNU General Public License version 2 only.
;=============================================================================

FD_REG_CTRL     equ     04h             ; DMA direction and start, protocol
FD_REG_INTRQ    equ     0Bh             ; bit 7: the controller's interrupt
FD_REG_MEMADDR  equ     28h             ; the host's buffer address
FD_REG_DMAADDR  equ     2Ch             ; the controller's buffer address
FD_REG_DMASIZE  equ     30h             ; its transfer length less one
FD_REG_DOR      equ     42h
FD_REG_MSR      equ     44h
FD_REG_DATA     equ     45h
FD_REG_DIR      equ     47h             ; reads the change line, writes CCR
FD_REG_MEMORY   equ     80h

FD_CTRL_WRITE   equ     24h             ; buffer to drive, set up
FD_CTRL_READ    equ     25h             ; drive to buffer, set up
FD_CTRL_GO      equ     02h             ; start the DMA

FD_DOR_OFF      equ     0Ch             ; drive 0, DMA, not in reset
FD_DOR_ON       equ     1Ch             ; and motor 0

FD_CMD_READ     equ     46h             ; MFM
FD_CMD_WRITE    equ     45h
FD_CMD_VERIFY   equ     0               ; a read that keeps the data

FD_SPINUP       equ     10              ; ticks: about half a second
FD_IDLE         equ     37              ; motor off after about 2 s
FD_SEEK_TICKS   equ     55              ; seek and recalibrate: 3 s
FD_ID_TICKS     equ     19              ; READ ID: 1 s
FD_TRIES        equ     3

SNS_FORMAT      equ     03h | (31h << 8)        ; medium format corrupted

dev_fdd         db      0               ; the drive is the pod's floppy
fd_dtype        db      7               ; EEPROM: drive 0 type (7 = 3.5" HD)
fd_ready        db      0               ; controller set up
fd_motor        db      0               ; motor on
fd_idle         db      0               ; ticks to motor off
fd_known        db      0               ; a medium is known
fd_ua           db      0               ; UNIT ATTENTION to report
fd_rate         db      0               ; CCR
fd_spt          db      18
fd_gpl          db      1Bh
fd_cyl          db      0FFh            ; where the head is, 0FFh unknown
fd_wp           db      0               ; write protected
fd_cap          dd      0               ; sectors
fd_sns          dd      0               ; sense of the last command
fd_op           db      0
fd_lba          dd      0
fd_left         dd      0
fd_n            dw      0               ; sectors of this transfer
fd_c            db      0
fd_h            db      0
fd_s            db      0
fd_try          db      0
fd_res          times 7 db 0            ; ST0 ST1 ST2 C H R N
fd_addr         times 3 db 0
fd_old1c        dd      0
fd_rates        db      0, 2, 3, 0FFh   ; 500, 250 kbit/s, 1 Mbit/s

;-----------------------------------------------------------------------------
; fdc_exec: the SCSI command cdb_buf with the request of dev_exec
;-----------------------------------------------------------------------------
fdc_exec:
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
        cmp     byte [x_target], 0      ; drive 0 only
        jne     .out
        mov     al, [cdb_buf]
        cmp     al, SCSI_REQUEST_SENSE
        je      .rsense
        mov     dword [fd_sns], 0       ; a new command clears the sense
        cmp     al, SCSI_INQUIRY
        je      .inquiry
        or      al, al                  ; TEST UNIT READY: the medium
        jz      .medium
        mov     si, ata_nops            ; nothing to do on the drive
.nop:   cmp     byte [si], 0FFh
        je      .medium
        cmp     al, [si]
        je      .good
        inc     si
        jmp     .nop

        ; everything else needs the medium
.medium:
        call    bpck_connect
        call    fd_media
        jc      .sense_d
        mov     al, [cdb_buf]
        cmp     al, 00h                 ; TEST UNIT READY
        je      .good_d
        cmp     al, 25h                 ; READ CAPACITY
        je      .rcap
        cmp     al, 1Ah                 ; MODE SENSE (6)
        je      .msense
        cmp     al, 5Ah                 ; MODE SENSE (10)
        je      .msense
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
        mov     eax, SNS_BAD_OPCODE
        jmp     .sense_d

.rsense:
        mov     eax, [fd_sns]
        mov     dword [fd_sns], 0
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
        mov     si, fd_inquiry
        mov     cx, 36
        rep     movsb
        mov     si, ata_reply
        mov     cx, 36
        call    ata_give
        jmp     .good

.rcap:  call    fd_done
        mov     eax, [fd_cap]
        dec     eax
        BSWAP_EAX
        mov     [ata_reply], eax
        mov     dword [ata_reply+4], 00020000h  ; 512 byte blocks
        mov     si, ata_reply
        mov     cx, 8
        call    ata_give
        jmp     .good

.msense:
        call    fd_wprot
        call    fd_done
        ; header, and a block descriptor unless DBD; no mode pages
        push    cs
        pop     es
        mov     di, ata_reply
        push    di
        xor     eax, eax
        mov     cx, 16 / 4
        rep     stosd
        pop     di
        mov     eax, [fd_cap]
        BSWAP_EAX                       ; density 0, blocks
        mov     bl, [fd_wp]             ; WP in the device specific byte
        cmp     byte [cdb_buf], 1Ah
        jne     .ms10
        mov     [di+2], bl
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
.ms10:  mov     [di+3], bl
        mov     cx, 8
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

.read6: mov     byte [fd_op], FD_CMD_READ
        jmp     .rw6
.write6:
        mov     byte [fd_op], FD_CMD_WRITE
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
.read:  mov     byte [fd_op], FD_CMD_READ
        jmp     .rw10
.write: mov     byte [fd_op], FD_CMD_WRITE
        jmp     .rw10
.verify:
        test    byte [cdb_buf+1], 02h   ; BYTCHK: compare with host data
        jnz     .badfield_d
        mov     byte [fd_op], FD_CMD_VERIFY
.rw10:  mov     eax, [cdb_buf+2]
        BSWAP_EAX
        movzx   ecx, word [cdb_buf+7]
        xchg    cl, ch
.rw:    mov     [fd_lba], eax
        mov     [fd_left], ecx
        mov     ebx, [fd_cap]
        sub     ebx, eax
        jb      .range_d
        cmp     ebx, ecx
        jb      .range_d
        cmp     byte [fd_op], FD_CMD_VERIFY
        je      .rwgo
        mov     eax, [x_len]            ; what the buffer holds
        shr     eax, 9
        cmp     eax, ecx
        jae     .rwgo
        mov     [fd_left], eax
        mov     byte [x_over], 1
.rwgo:  cmp     byte [fd_op], FD_CMD_WRITE
        jne     .rwgo2
        call    fd_wprot
        mov     eax, SNS_WPROT
        cmp     byte [fd_wp], 0
        jne     .sense_d
.rwgo2: call    fd_rw
        jc      .sense_d
        jmp     .good_d

.range_d:
        mov     eax, SNS_RANGE
        jmp     .sense_d
.badfield_d:
        mov     eax, SNS_BAD_FIELD
.sense_d:
        call    fd_done
        jmp     .sense
.badfield:
        mov     eax, SNS_BAD_FIELD
.sense: mov     [fd_sns], eax
        shl     al, 4
        mov     [x_err], al
        mov     byte [x_status], STATUS_CHKCOND
        jmp     .good
.good_d:
        call    fd_done
.good:  mov     al, XR_OK
.out:   pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret

fd_inquiry      db      00h, 80h, 02h, 02h, 31, 0, 0, 0
                db      'MICROSOL'
                db      'BACKPACK FLOPPY '
                db      'FDC '

; the end of a command on the drive: the motor runs on for a while
fd_done:
        push    eax
        cmp     byte [fd_motor], 0
        je      .d
        mov     byte [fd_idle], FD_IDLE
.d:     call    bpck_disconnect
        pop     eax
        ret

;-----------------------------------------------------------------------------
; The controller, through the pod (connected, DX = base)
;-----------------------------------------------------------------------------

; AL = command or parameter byte for the controller.  CF on timeout.
fd_out: push    eax
        push    ebx
        push    esi
        mov     bl, al
        call    get_ticks
        mov     esi, eax
.l:     mov     ah, FD_REG_MSR
        call    bpck_rd
        and     al, 0C0h
        cmp     al, 80h                 ; RQM, to the controller
        je      .w
        call    get_ticks
        sub     eax, esi
        cmp     eax, CMD_TICKS
        jbe     .l
        stc
        jmp     .o
.w:     mov     ah, FD_REG_DATA
        mov     al, bl
        call    bpck_wr
        clc
.o:     pop     esi
        pop     ebx
        pop     eax
        ret

; -> AL = a result byte from the controller.  CF on timeout.
fd_in:  push    ebx
        push    esi
        call    get_ticks
        mov     esi, eax
.l:     mov     ah, FD_REG_MSR
        call    bpck_rd
        and     al, 0C0h
        cmp     al, 0C0h                ; RQM, from the controller
        je      .r
        call    get_ticks
        sub     eax, esi
        cmp     eax, CMD_TICKS
        jbe     .l
        stc
        jmp     .o
.r:     mov     ah, FD_REG_DATA
        call    bpck_rd
        clc
.o:     pop     esi
        pop     ebx
        ret

; wait for the controller's interrupt, EBX ticks.  CF on timeout.
fd_wait_int:
        push    eax
        push    esi
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.l:     mov     ah, FD_REG_INTRQ
        call    bpck_rd
        test    al, 80h
        jnz     .ok
        call    get_ticks
        sub     eax, esi
        cmp     eax, ebx
        jbe     .l
        popf
        stc
        jmp     .o
.ok:    popf
        clc
.o:     pop     esi
        pop     eax
        ret

; wait EBX ticks
fd_sleep:
        push    eax
        push    esi
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.l:     call    get_ticks
        sub     eax, esi
        cmp     eax, ebx
        jb      .l
        popf
        pop     esi
        pop     eax
        ret

; SENSE INTERRUPT STATUS -> AL = ST0, AH = cylinder.  CF on timeout.
fd_sense_int:
        mov     al, 08h
        call    fd_out
        jc      .o
        call    fd_in
        jc      .o
        mov     [fd_res], al
        call    fd_in
        jc      .o
        mov     ah, al
        mov     al, [fd_res]
.o:     ret

; the seven result bytes of a read, write or READ ID into fd_res
fd_results:
        push    cx
        push    di
        mov     di, fd_res
        mov     cx, 7
.l:     call    fd_in
        jc      .o
        mov     [di], al
        inc     di
        loop    .l
.o:     pop     di
        pop     cx
        ret

; three byte pod register BL = EAX (addressed once, low byte first)
fd_set3:
        push    es
        push    di
        push    cx
        mov     [fd_addr], ax
        shr     eax, 16
        mov     [fd_addr+2], al
        push    cs
        pop     es
        mov     di, fd_addr
        mov     cx, 3
        mov     al, bl
        call    bk_sput
        pop     cx
        pop     di
        pop     es
        ret

; DMA direction and start in register 04h: AL | the protocol bits
fd_ctrl:
        cmp     byte [lpt_mode], MODE_PS2
        jne     .w
        or      al, 10h
.w:     mov     ah, FD_REG_CTRL
        jmp     bpck_wr

;-----------------------------------------------------------------------------
; fdc_probe: is there a 765 behind the pod (connected)?  Reset it: it must
; take a command and then hand back the status of the reset.  CF if not.
;-----------------------------------------------------------------------------
fdc_probe:
        mov     byte [fd_motor], 0
        call    fd_reset
        ret

; reset the controller and set it up.  CF if it does not answer.
fd_reset:
        push    ax
        push    cx
        push    ebx
        mov     byte [fd_ready], 0
        mov     byte [fd_cyl], 0FFh
        mov     al, FD_DOR_OFF
        cmp     byte [fd_motor], 0
        je      .m
        mov     al, FD_DOR_ON
.m:     push    ax
        and     al, 0FBh                ; reset (bit 2 clear)
        mov     ah, FD_REG_DOR
        call    bpck_wr
        mov     ebx, 1
        call    fd_sleep
        pop     ax
        mov     ah, FD_REG_DOR
        call    bpck_wr
        mov     ebx, FD_ID_TICKS
        call    fd_wait_int
        jc      .no
        mov     cx, 4                   ; one status per drive
.si:    call    fd_sense_int
        jc      .no
        and     al, 0C0h
        cmp     al, 0C0h                ; ready changed: after a reset
        jne     .no
        loop    .si
        mov     al, 03h                 ; SPECIFY: 3 ms step, longest
        call    fd_out                  ; unload, 2 ms load, DMA
        jc      .no
        mov     al, 0DFh
        call    fd_out
        jc      .no
        mov     al, 02h
        call    fd_out
        jc      .no
        mov     ah, FD_REG_DIR          ; CCR
        mov     al, [fd_rate]
        call    bpck_wr
        mov     byte [fd_ready], 1
        clc
        jmp     .o
.no:    stc
.o:     pop     ebx
        pop     cx
        pop     ax
        ret

; the motor on, and up to speed
fd_motor_on:
        cmp     byte [fd_motor], 0
        jne     .o
        push    ax
        push    ebx
        mov     ax, (FD_REG_DOR << 8) | FD_DOR_ON
        call    bpck_wr
        mov     byte [fd_motor], 1
        mov     ebx, FD_SPINUP
        call    fd_sleep
        pop     ebx
        pop     ax
.o:     mov     byte [fd_idle], 0       ; not while we work
        ret

; RECALIBRATE, twice if the head was beyond 77 steps.  CF on failure.
fd_recal:
        push    ax
        push    cx
        push    ebx
        mov     cx, 2
.t:     mov     al, 07h
        call    fd_out
        jc      .no
        xor     al, al
        call    fd_out
        jc      .no
        mov     ebx, FD_SEEK_TICKS
        call    fd_wait_int
        jc      .no
        call    fd_sense_int
        jc      .no
        and     al, 0F0h
        cmp     al, 20h                 ; seek end, normal
        je      .ok
        loop    .t
.no:    mov     byte [fd_cyl], 0FFh
        stc
        jmp     .o
.ok:    mov     byte [fd_cyl], 0
        clc
.o:     pop     ebx
        pop     cx
        pop     ax
        ret

; SEEK to cylinder AL (head AH).  CF on failure.
fd_seek:
        cmp     al, [fd_cyl]
        je      .same
        push    ax
        push    bx
        push    cx
        push    ebx
        mov     bx, ax
        mov     al, 0Fh
        call    fd_out
        jc      .no
        mov     al, bh
        shl     al, 2
        call    fd_out
        jc      .no
        mov     al, bl
        call    fd_out
        jc      .no
        mov     ebx, FD_SEEK_TICKS
        call    fd_wait_int
        jc      .no
        call    fd_sense_int
        jc      .no
        and     al, 0E0h
        cmp     al, 20h                 ; seek end, normal
        jne     .no
        mov     [fd_cyl], ah
        mov     cx, 15000 / 15          ; head settle, 15 ms
        call    delay15
        clc
        jmp     .o
.no:    mov     byte [fd_cyl], 0FFh
        stc
.o:     pop     ebx
        pop     cx
        pop     bx
        pop     ax
        ret
.same:  clc
        ret

; READ ID on head AH of this cylinder.  CF if no sector header is found.
fd_readid:
        push    ax
        push    ebx
        mov     al, 4Ah
        call    fd_out
        jc      .no
        mov     al, ah
        shl     al, 2
        call    fd_out
        jc      .no
        mov     ebx, FD_ID_TICKS
        call    fd_wait_int
        jc      .lost
        call    fd_results
        jc      .lost
        test    byte [fd_res], 0C0h
        jnz     .no
        clc
        jmp     .o
.lost:  call    fd_reset                ; the command did not end
.no:    stc
.o:     pop     ebx
        pop     ax
        ret

; SENSE DRIVE STATUS -> fd_wp
fd_wprot:
        push    ax
        mov     byte [fd_wp], 0
        mov     al, 04h
        call    fd_out
        jc      .o
        xor     al, al
        call    fd_out
        jc      .o
        call    fd_in
        jc      .o
        and     al, 40h                 ; ST3: write protected
        jz      .o
        mov     byte [fd_wp], 80h
.o:     pop     ax
        ret

;-----------------------------------------------------------------------------
; fd_media: the medium for the command (connected).  The change line set
; means the disk went out: the head is stepped to clear it, which only
; works with a disk in.  A new disk is measured with READ ID at the data
; rates the drive takes.  CF and EAX = sense when there is no usable one.
;-----------------------------------------------------------------------------
fd_media:
        call    fd_motor_on
        cmp     byte [fd_ready], 0
        jne     .rdy
        call    fd_reset
        jc      .fail
.rdy:   mov     ah, FD_REG_DIR
        call    bpck_rd
        test    al, 80h                 ; disk change
        jnz     .changed
        cmp     byte [fd_known], 0
        jne     .ok
        jmp     .detect
.changed:
        mov     ax, 0001h               ; step out and back
        call    fd_seek
        call    fd_recal
        mov     ah, FD_REG_DIR
        call    bpck_rd
        test    al, 80h
        jnz     .none                   ; still set: no disk
        mov     al, [fd_known]          ; a disk seen before: report it
        mov     [fd_ua], al
        mov     byte [fd_known], 0
.detect:
        call    fd_detect
        jc      .unformatted
        cmp     byte [fd_ua], 0
        je      .ok
        mov     byte [fd_ua], 0
        mov     eax, SNS_CHANGED
        stc
        ret
.ok:    clc
        ret
.none:  mov     byte [fd_known], 0
        mov     eax, SNS_NO_MEDIUM
        stc
        ret
.unformatted:
        mov     eax, SNS_FORMAT
        stc
        ret
.fail:  mov     eax, SNS_ABORTED
        stc
        ret

; the format of the disk: READ ID on cylinder 0 at each data rate
fd_detect:
        push    ax
        push    si
        cmp     byte [fd_cyl], 0
        je      .c0
        call    fd_recal
        jc      .no
.c0:    mov     si, fd_rates
.r:     mov     al, [si]
        cmp     al, 0FFh
        je      .no
        inc     si
        mov     [fd_rate], al
        mov     ah, FD_REG_DIR          ; CCR
        call    bpck_wr
        xor     ah, ah
        call    fd_readid
        jc      .r
        ; sectors per track: by the data rate, and for 500 kbit/s by
        ; the drive (a 5.25" 1.2 MB drive has 15)
        mov     al, [fd_rate]
        mov     ah, 36
        mov     byte [fd_gpl], 1Bh
        cmp     al, 3
        je      .spt
        mov     ah, 9
        mov     byte [fd_gpl], 2Ah
        cmp     al, 2
        je      .spt
        mov     byte [fd_gpl], 1Bh
        mov     ah, 18
        mov     al, [fd_dtype]
        cmp     al, 2
        je      .hd525
        cmp     al, 3
        jne     .spt
.hd525: mov     ah, 15
.spt:   mov     [fd_spt], ah
        movzx   eax, ah
        imul    eax, eax, 2 * 80
        mov     [fd_cap], eax
        mov     byte [fd_known], 1
        clc
        jmp     .o
.no:    mov     byte [fd_known], 0
        stc
.o:     pop     si
        pop     ax
        ret

;-----------------------------------------------------------------------------
; fd_rw: [fd_op] of [fd_left] sectors at [fd_lba], data at x_buf
; (connected, medium known).  A track side at a time through the pod's
; buffer.  CF and EAX = sense on failure.
;-----------------------------------------------------------------------------
fd_rw:
.next:  cmp     dword [fd_left], 0
        jne     .chunk
        clc
        ret
.chunk: mov     eax, [fd_lba]           ; LBA -> C, H, S
        xor     edx, edx
        movzx   ecx, byte [fd_spt]
        div     ecx                     ; EAX = track, EDX = sector - 1
        inc     dl
        mov     [fd_s], dl
        mov     ecx, edx
        mov     edx, eax
        shr     eax, 1
        mov     [fd_c], al
        and     dl, 1
        mov     [fd_h], dl
        movzx   eax, byte [fd_spt]      ; to the end of this track side
        sub     al, cl
        inc     al
        cmp     eax, [fd_left]
        jbe     .n
        mov     eax, [fd_left]
.n:     mov     [fd_n], ax
        mov     dx, [lpt_base]
        mov     byte [fd_try], FD_TRIES
.try:   call    fd_xfer
        jnc     .done
        mov     dx, [lpt_base]
        cmp     eax, SNS_WPROT
        je      .fail
        push    eax
        call    fd_reset
        pop     eax
        dec     byte [fd_try]
        jnz     .try
.fail:  stc
        ret
.done:  movzx   eax, word [fd_n]
        add     [fd_lba], eax
        sub     [fd_left], eax
        jmp     .next

; one transfer of fd_n sectors at fd_c/fd_h/fd_s.  CF and EAX = sense.
fd_xfer:
        mov     al, [fd_c]
        mov     ah, [fd_h]
        call    fd_seek
        jc      .seekerr
        movzx   eax, word [fd_n]        ; the length, less one
        shl     eax, 9
        dec     eax
        push    eax
        cmp     byte [fd_op], FD_CMD_WRITE
        je      .wsetup
        mov     al, FD_CTRL_READ
        call    fd_ctrl
        jmp     .dma
.wsetup:
        mov     al, FD_CTRL_WRITE       ; the sectors into the pod's buffer
        call    fd_ctrl
        xor     eax, eax
        mov     bl, FD_REG_MEMADDR
        call    fd_set3
        les     di, [x_buf]         ; one stream: the pod counts on
        mov     cx, [fd_n]
        shl     cx, 9
        mov     al, FD_REG_MEMORY
        call    bk_sput
.dma:   xor     eax, eax
        mov     bl, FD_REG_DMAADDR
        call    fd_set3
        pop     eax
        mov     bl, FD_REG_DMASIZE
        call    fd_set3
        mov     al, FD_CTRL_READ | FD_CTRL_GO
        cmp     byte [fd_op], FD_CMD_WRITE
        jne     .go
        mov     al, FD_CTRL_WRITE | FD_CTRL_GO
.go:    call    fd_ctrl
        mov     al, [fd_op]             ; the command
        cmp     al, FD_CMD_VERIFY
        jne     .cmd
        mov     al, FD_CMD_READ
.cmd:   call    fd_out
        jc      .lost
        mov     al, [fd_h]
        shl     al, 2
        call    fd_out
        jc      .lost
        mov     al, [fd_c]
        call    fd_out
        jc      .lost
        mov     al, [fd_h]
        call    fd_out
        jc      .lost
        mov     al, [fd_s]
        call    fd_out
        jc      .lost
        mov     al, 2                   ; 512 bytes
        call    fd_out
        jc      .lost
        mov     al, [fd_s]              ; the last sector
        add     al, [fd_n]
        dec     al
        call    fd_out
        jc      .lost
        mov     al, [fd_gpl]
        call    fd_out
        jc      .lost
        mov     al, 0FFh
        call    fd_out
        jc      .lost
        mov     ebx, [tmo_ticks]
        call    fd_wait_int
        jc      .lost
        call    fd_results
        jc      .lost
        test    byte [fd_res], 0C0h
        jnz     .error
        ; read: the sectors out of the pod's buffer
        cmp     byte [fd_op], FD_CMD_READ
        jne     .moved
        xor     eax, eax
        mov     bl, FD_REG_MEMADDR
        call    fd_set3
        les     di, [x_buf]
        mov     cx, [fd_n]
        shl     cx, 9
        mov     al, FD_REG_MEMORY
        call    bk_sget
.moved: cmp     byte [fd_op], FD_CMD_VERIFY
        je      .ok
        movzx   eax, word [fd_n]
        shl     ax, 5
        add     [x_buf+2], ax
        shl     eax, 4
        add     [x_xfer], eax
.ok:    clc
        ret
.seekerr:
        mov     eax, SNS_NOT_FOUND
        stc
        ret
.lost:  mov     eax, SNS_ABORTED
        stc
        ret
.error: mov     al, [fd_res+1]          ; ST1
        mov     ah, [fd_res+2]          ; ST2
        mov     ebx, eax
        mov     eax, SNS_WPROT
        test    bl, 02h                 ; not writable
        jnz     .e
        mov     eax, SNS_UNC
        test    bl, 20h                 ; CRC error
        jnz     .e
        test    bh, 20h
        jnz     .e
        mov     eax, SNS_NOT_FOUND      ; no address mark, no data
        test    bl, 05h
        jnz     .e
        test    bh, 01h
        jnz     .e
        mov     eax, SNS_ABORTED
.e:     stc
        ret

;-----------------------------------------------------------------------------
; INT 1Ch: the motor off when the drive has been idle for a while
;-----------------------------------------------------------------------------
fd_timer:
        cmp     byte [cs:fd_idle], 0
        je      .chain
        dec     byte [cs:fd_idle]
        jnz     .chain
        cmp     byte [cs:in_aspi], 0
        je      .off
        mov     byte [cs:fd_idle], 1    ; busy: the next tick
        jmp     .chain
.off:   pushf
        cli
        push    ax
        push    bx
        push    dx
        push    ds
        push    cs
        pop     ds
        mov     dx, [lpt_base]
        call    bpck_connect
        mov     ax, (FD_REG_DOR << 8) | FD_DOR_OFF
        call    bpck_wr
        call    bpck_disconnect
        mov     byte [fd_motor], 0
        pop     ds
        pop     dx
        pop     bx
        pop     ax
        popf
.chain: jmp     far [cs:fd_old1c]
