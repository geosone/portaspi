;=============================================================================
; mcd.inc - SCSI commands on the Mitsumi CD-ROM drive of the first Micro
; Solutions BackPack CD-ROMs (160550, 162550): a Mitsumi FX001 or FX001D
; behind the BackPack link
;
; Register 40h of the pod is the drive's command and data register, 41h
; its flags: bit 2 clear when a status byte waits, bit 1 clear when data
; waits; a write to it resets the drive.  The way Grant Guenther's Linux
; bpmcd.c uses it: a command, its parameters, then wait until a flag
; drops; a sector read takes the 2048 bytes and then the status.
;
; The drive is offered as SCSI ID 0, a CD-ROM drive:
;   TEST UNIT READY                      the drive's status
;   INQUIRY                              "MITSUMI CRMC-FX001(D)"
;   READ CAPACITY                        the lead-out of the disc
;   READ (10)                            a sector at a time, 2048 bytes
;   READ TOC (format 0)                  the first track and the lead-out:
;                                        the drive's disc information has
;                                        no other track's start
;   REZERO, SEEK, START STOP, PREVENT    nothing to do
;   ALLOW, RESERVE, RELEASE, MODE SELECT
;   REQUEST SENSE                        the sense of the last command
; A changed disc is reported once as UNIT ATTENTION.
;
; License: GNU General Public License version 2 only.
;=============================================================================

MC_REG_DATA     equ     40h             ; commands, parameters, results, data
MC_REG_FLAGS    equ     41h             ; bit 2: no status, bit 1: no data
MC_FL_NOSTAT    equ     04h
MC_FL_NODATA    equ     02h

MC_CMD_INFO     equ     10h             ; first and last track, lead-out
MC_CMD_STATUS   equ     40h
MC_CMD_READ     equ     0C0h            ; + 1 at double speed
MC_CMD_VERSION  equ     0DCh
MC_CMD_CLOSE    equ     0F8h

MC_ST_ERROR     equ     07h             ; command check, play, error
MC_ST_CHANGE    equ     20h
MC_ST_READY     equ     40h
MC_ST_OPEN      equ     80h

MC_SECTOR       equ     2048

dev_mcd         db      0               ; the drive is the pod's Mitsumi
mc_speed        db      0               ; 1 for an FX001D (double speed)
mc_ua           db      0               ; this change was reported
mc_sns          dd      0               ; sense of the last command
mc_lba          dd      0
mc_left         dd      0
mc_info         times 8 db 0            ; first, last, lead-out, track 1
mc_toc          times 20 db 0

;-----------------------------------------------------------------------------
; mcd_exec: the SCSI command cdb_buf with the request of dev_exec
;-----------------------------------------------------------------------------
mcd_exec:
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
        mov     dword [mc_sns], 0       ; a new command clears the sense
        cmp     al, SCSI_INQUIRY
        je      .inquiry

        ; everything else asks the drive first
        call    bpck_connect
        call    mc_ready
        jc      .sense_d
        mov     al, [cdb_buf]
        cmp     al, 00h                 ; TEST UNIT READY
        je      .good_d
        cmp     al, 25h                 ; READ CAPACITY
        je      .rcap
        cmp     al, 28h                 ; READ (10)
        je      .read
        cmp     al, 43h                 ; READ TOC
        je      .toc
        mov     si, ata_nops            ; nothing to do on the drive
.nop:   cmp     byte [si], 0FFh
        je      .bad
        cmp     al, [si]
        je      .good_d
        inc     si
        jmp     .nop
.bad:   mov     eax, SNS_BAD_OPCODE
        jmp     .sense_d

.rsense:
        mov     eax, [mc_sns]
        mov     dword [mc_sns], 0
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
        mov     si, mc_inquiry
        mov     cx, 36
        rep     movsb
        cmp     byte [mc_speed], 0
        je      .inq
        mov     byte [ata_reply+26], 'D'
.inq:   mov     si, ata_reply
        mov     cx, 36
        call    ata_give
        jmp     .good

.rcap:  call    mc_disc_info
        jc      .sense_d
        call    bpck_disconnect
        mov     si, mc_info+2           ; the lead-out
        call    mc_msf_lba
        dec     eax                     ; the last block
        BSWAP_EAX
        mov     [ata_reply], eax
        mov     dword [ata_reply+4], 00080000h  ; 2048 byte blocks
        mov     si, ata_reply
        mov     cx, 8
        call    ata_give
        jmp     .good

.toc:   call    mc_disc_info
        jc      .sense_d
        call    bpck_disconnect
        test    byte [cdb_buf+2], 0Fh   ; format 0 only
        jnz     .badfield
        test    byte [cdb_buf+9], 0C0h
        jnz     .badfield
        push    cs
        pop     es
        mov     di, mc_toc+4
        mov     al, [mc_info]           ; the first track
        call    mc_bin
        mov     bl, al
        mov     al, [cdb_buf+6]         ; starting track
        cmp     al, bl
        ja      .tocl
        mov     si, mc_info+5
        call    mc_toc_entry
.tocl:  cmp     byte [cdb_buf+6], 0AAh
        ja      .badfield
        push    bx
        mov     bl, 0AAh                ; the lead-out
        mov     si, mc_info+2
        call    mc_toc_entry
        pop     bx
        mov     ax, di
        sub     ax, mc_toc+2            ; data length
        xchg    al, ah
        mov     [mc_toc], ax
        mov     [mc_toc+2], bl          ; first and last track: the one
        mov     [mc_toc+3], bl          ; whose start the drive tells
        mov     si, mc_toc
        mov     cx, di
        sub     cx, si
        call    ata_give
        jmp     .good

.read:  mov     eax, [cdb_buf+2]
        BSWAP_EAX
        movzx   ecx, word [cdb_buf+7]
        xchg    cl, ch
        mov     [mc_lba], eax
        mov     [mc_left], ecx
        mov     eax, [x_len]            ; what the buffer holds
        shr     eax, 11
        cmp     eax, ecx
        jae     .rd
        mov     [mc_left], eax
        mov     byte [x_over], 1
.rd:    cmp     dword [mc_left], 0
        je      .good_d
        call    mc_read
        jc      .sense_d
        inc     dword [mc_lba]
        dec     dword [mc_left]
        add     word [x_buf+2], MC_SECTOR / 16
        add     dword [x_xfer], MC_SECTOR
        jmp     .rd

.badfield:
        mov     eax, SNS_BAD_FIELD
        jmp     .sense
.sense_d:
        call    bpck_disconnect
.sense: mov     [mc_sns], eax
        shl     al, 4
        mov     [x_err], al
        mov     byte [x_status], STATUS_CHKCOND
        jmp     .good
.good_d:
        call    bpck_disconnect
.good:  mov     al, XR_OK
.out:   pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret

mc_inquiry      db      05h, 80h, 02h, 02h, 31, 0, 0, 0
                db      'MITSUMI '
                db      'CRMC-FX001      '
                db      'BPCK'

;-----------------------------------------------------------------------------
; The drive, through the pod (connected, DX = base)
;-----------------------------------------------------------------------------

; wait until the drive has a status byte or data for us, EBX ticks.
; -> AL = the flags.  CF on timeout.
mc_wait:
        push    esi
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.l:     mov     ah, MC_REG_FLAGS
        call    bpck_rd
        and     al, MC_FL_NOSTAT | MC_FL_NODATA
        cmp     al, MC_FL_NOSTAT | MC_FL_NODATA
        jne     .ok
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
        ret

; -> AL = the next status or result byte.  CF if none comes.
mc_byte:
        push    ebx
        mov     ebx, CMD_TICKS
        call    mc_wait
        jc      .o
        test    al, MC_FL_NOSTAT
        stc
        jnz     .o
        mov     ah, MC_REG_DATA
        call    bpck_rd
        clc
.o:     pop     ebx
        ret

; AL = a command or parameter byte for the drive
mc_out: mov     ah, MC_REG_DATA
        jmp     bpck_wr

; the drive's status -> AL.  CF if it does not answer.
mc_status:
        mov     al, MC_CMD_STATUS
        call    mc_out
        jmp     mc_byte

; a disc in a drive that is ready, once a change was reported.  An open
; tray is closed, as bpmcd.c and BPCDDRV.SYS do; after a change the drive
; gets the command timeout to come up.  CF and EAX = the sense if not.
mc_ready:
        push    ebx
        push    esi
        call    get_ticks
        mov     esi, eax
        xor     bl, bl                  ; the status bits seen
.st:    call    mc_status
        jc      .lost
        mov     bh, al
        or      bl, al
        test    bh, MC_ST_OPEN
        jz      .closed
        mov     al, MC_CMD_CLOSE
        call    mc_out
        call    mc_byte
        jc      .lost
        or      bl, al
        jmp     .wait
.closed:
        test    bh, MC_ST_READY
        jnz     .ready
        test    bl, MC_ST_CHANGE        ; a new disc may still spin up
        jz      .none
.wait:  call    get_ticks
        sub     eax, esi
        cmp     eax, [tmo_ticks]
        jbe     .st
.none:  mov     eax, SNS_NO_MEDIUM
        jmp     .fail
.ready: test    bl, MC_ST_CHANGE        ; a new disc: UNIT ATTENTION once,
        jz      .same                   ; the drive sets the bit in more
        cmp     byte [mc_ua], 0         ; than one status after a change
        jne     .ok
        mov     byte [mc_ua], 1
        mov     eax, SNS_CHANGED
        jmp     .fail
.same:  mov     byte [mc_ua], 0
.ok:    clc
        jmp     .o
.lost:  mov     eax, SNS_ABORTED
.fail:  stc
.o:     pop     esi
        pop     ebx
        ret

; the disc information into mc_info (BCD).  CF and EAX = the sense if not.
mc_disc_info:
        push    cx
        push    di
        mov     al, MC_CMD_INFO
        call    mc_out
        call    mc_byte                 ; the status
        jc      .lost
        test    al, MC_ST_ERROR
        jnz     .err
        mov     di, mc_info
        mov     cx, 8
.l:     call    mc_byte
        jc      .lost
        mov     [di], al
        inc     di
        loop    .l
        clc
        jmp     .o
.err:   mov     eax, SNS_NO_MEDIUM
        stc
        jmp     .o
.lost:  mov     eax, SNS_ABORTED
        stc
.o:     pop     di
        pop     cx
        ret

; the sector mc_lba into x_buf.  CF and EAX = the sense if not.
mc_read:
        push    ebx
        push    cx
        push    di
        push    es
        mov     eax, [mc_lba]
        add     eax, 150                ; the drive counts from 00:02:00
        xor     edx, edx
        mov     ebx, 75
        div     ebx
        mov     ch, dl                  ; frame
        xor     edx, edx
        mov     ebx, 60
        div     ebx                     ; AL = minute, DL = second
        mov     cl, dl
        mov     bl, al
        mov     dx, [lpt_base]
        mov     al, MC_CMD_READ
        add     al, [mc_speed]
        call    mc_out
        mov     al, bl
        call    mc_bcd
        call    mc_out
        mov     al, cl
        call    mc_bcd
        call    mc_out
        mov     al, ch
        call    mc_bcd
        call    mc_out
        xor     al, al                  ; one sector
        call    mc_out
        xor     al, al
        call    mc_out
        mov     al, 1
        call    mc_out
        mov     ebx, [tmo_ticks]
        call    mc_wait
        jc      .lost
        test    al, MC_FL_NODATA
        jnz     .status                 ; a status first: the read failed
        les     di, [x_buf]
        mov     cx, MC_SECTOR
        mov     al, MC_REG_DATA
        call    bk_sget
.status:
        call    mc_byte
        jc      .lost
        test    al, MC_ST_ERROR
        jnz     .err
        clc
        jmp     .o
.err:   mov     eax, SNS_UNC
        stc
        jmp     .o
.lost:  mov     eax, SNS_ABORTED
        stc
.o:     pop     es
        pop     di
        pop     cx
        pop     ebx
        ret

; one READ TOC descriptor at ES:DI for track BL, address at SI (BCD MSF)
mc_toc_entry:
        mov     byte [di], 0
        mov     byte [di+1], 14h        ; Q: position, data track
        mov     [di+2], bl
        mov     byte [di+3], 0
        test    byte [cdb_buf+1], 02h   ; MSF
        jz      .lba
        mov     byte [di+4], 0
        mov     al, [si]
        call    mc_bin
        mov     [di+5], al
        mov     al, [si+1]
        call    mc_bin
        mov     [di+6], al
        mov     al, [si+2]
        call    mc_bin
        mov     [di+7], al
        jmp     .n
.lba:   call    mc_msf_lba
        BSWAP_EAX
        mov     [di+4], eax
.n:     add     di, 8
        ret

; BCD MSF at SI -> EAX = the logical block
mc_msf_lba:
        push    ebx
        mov     al, [si]                ; minutes
        call    mc_bin
        movzx   ebx, al
        imul    ebx, ebx, 60
        mov     al, [si+1]              ; seconds
        call    mc_bin
        movzx   eax, al
        add     ebx, eax
        imul    ebx, ebx, 75
        mov     al, [si+2]              ; frames
        call    mc_bin
        movzx   eax, al
        add     eax, ebx
        sub     eax, 150
        pop     ebx
        ret

; AL (0-99) -> BCD
mc_bcd: aam
        shl     ah, 4
        or      al, ah
        ret

; AL BCD -> binary
mc_bin: mov     ah, al
        shr     ah, 4
        and     al, 0Fh
        aad
        ret

;-----------------------------------------------------------------------------
; mcd_probe: is there a Mitsumi drive behind the pod (connected)?  Reset
; it as bpmcd.c does; it must then tell its status and its version.
; CF if not.
;-----------------------------------------------------------------------------
mcd_probe:
        push    ebx
        push    cx
        mov     ax, (MC_REG_FLAGS << 8) | 0     ; reset the drive
        call    bpck_wr
        mov     ebx, 2                  ; let it come up
        call    fd_sleep
        mov     cx, 16                  ; what it may still tell
.drain: mov     ah, MC_REG_FLAGS
        call    bpck_rd
        test    al, MC_FL_NOSTAT
        jnz     .st
        mov     ah, MC_REG_DATA
        call    bpck_rd
        loop    .drain
.st:    call    mc_status
        jc      .no
        mov     al, MC_CMD_VERSION
        call    mc_out
        call    mc_byte                 ; status
        jc      .no
        call    mc_byte                 ; 'D' for the double speed FX001D
        jc      .no
        cmp     al, 'D'
        jne     .v
        mov     byte [mc_speed], 1
.v:     call    mc_byte                 ; the version
        jc      .no
        clc
        jmp     .o
.no:    stc
.o:     pop     cx
        pop     ebx
        ret
