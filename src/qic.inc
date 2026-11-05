;=============================================================================
; qic.inc - SCSI tape commands on a QIC-117 floppy tape drive at the
; floppy controller of the PC (/F)
;
; A floppy tape drive sits on the floppy cable on a drive select line of
; its own.  It takes its commands as step pulses: the host seeks that many
; cylinders away and the drive counts the pulses.  It answers on the TRACK
; 0 line: after a report command each "report next bit" (two pulses)
; presents the next bit, read with SENSE DRIVE STATUS: an acknowledge, the
; bits low first, a stop bit.  Commands are kept apart by 3 ms.  This is
; the way Linux ftape (ftape-io.c, fdc-io.c) talks to the drive.
;
; The drive is offered as the first free SCSI ID, a sequential device:
;   TEST UNIT READY                      the drive's status
;   INQUIRY                              the drive's vendor
;   READ (6)                             variable blocks of a volume
;   SPACE                                filemarks (volumes), end of data
;   REWIND, LOAD UNLOAD                  the first volume, seek load point
;   MODE SENSE (6)                       write protection, variable blocks
;   READ BLOCK LIMITS                    1 to 65535 bytes
;   REQUEST SENSE                        the sense of the last command
; A new cartridge is reported once as UNIT ATTENTION.
;
; The tape is read in the QIC-80 volume format (QIC-80, QIC-113): the
; header segment (signature 0AA55AA55h, the geometry and the bad sector
; map), then the volume table in the first data segment, a 128 byte
; "VTBL" entry a volume with its first and last segment and its size.
; Each volume is a tape file: its data are the good sectors of its
; segments but the last three of each, which carry the Reed-Solomon
; parity; a filemark follows each volume.  A segment is 32 sectors of
; 1 KB; it sits at cylinder (segment mod segments a head) / segments a
; cylinder, head segment / segments a head, sector (segment mod segments
; a cylinder) * 32 + 1 of the controller's address space.
;
; Reading streams: the tape keeps running from one segment to the next.
; Before the first segment, or when the tape stopped, the driver finds
; its place with READ ID and gets there with SKIP SEGMENTS.  The data go
; through a 32 KB buffer after the resident part, by DMA channel 2.
;
; License: GNU General Public License version 2 only.
;=============================================================================

QT_DOR          equ     2               ; digital output register
QT_MSR          equ     4               ; main status register
QT_DATA         equ     5
QT_CCR          equ     7               ; configuration control (write)

QT_DOR_RUN      equ     0Ch             ; DMA, not in reset

; QIC-117 commands (pulses)
QIC_RESET       equ     1
QIC_FORWARD     equ     10              ; logical forward: stream
QIC_PHYS_REV    equ     11
QIC_PHYS_FWD    equ     12
QIC_TRACK       equ     13              ; seek head to track, a parameter
QIC_SKIP_REV    equ     25              ; skip segments, two nibbles
QIC_SKIP_FWD    equ     26
QIC_SKIPX_REV   equ     34              ; three nibbles
QIC_SKIPX_FWD   equ     35
QIC_NEXT_BIT    equ     2
QIC_STATUS      equ     6
QIC_ERROR       equ     7
QIC_CONFIG      equ     8
QIC_ROM         equ     9
QIC_LOAD_POINT  equ     14
QIC_STOP        equ     18
QIC_VENDOR      equ     32

; drive status
QS_READY        equ     01h
QS_ERROR        equ     02h
QS_CART         equ     04h
QS_WPROT        equ     08h
QS_NEWCART      equ     10h
QS_REFERENCED   equ     20h
QS_BOT          equ     40h
QS_EOT          equ     80h

QT_GAP          equ     200             ; 15 us periods: 3 ms between commands
QT_ACK_TRIES    equ     100             ; 3 ms each: the acknowledge
QT_INT_TICKS    equ     37              ; a seek: 2 s

SNS_QT_HW       equ     04h | (44h << 8)        ; internal target failure
SNS_QT_FORMAT   equ     03h | (30h << 8) | (02h << 16)  ; incompatible format
SNS_QT_READ     equ     03h | (11h << 8)        ; unrecovered read error
SNS_QT_FM       equ     00h | (00h << 8) | (01h << 16)  ; filemark
SNS_QT_EOD      equ     08h | (00h << 8) | (05h << 16)  ; end of data
SNS_QT_BOT      equ     00h | (00h << 8) | (04h << 16)  ; beginning of partition

QT_FL_FM        equ     80h             ; sense byte 2: filemark
QT_FL_EOM       equ     40h
QT_FL_ILI       equ     20h

QT_MAXBAD       equ     256             ; segments with bad sectors
QT_MAXVOL       equ     64
QT_TRIES        equ     8               ; to find a segment
QT_RW_TICKS     equ     55              ; a run of sectors: 3 s
QT_BUFSIZE      equ     32768
SNS_BECOMING    equ     02h | (04h << 8) | (01h << 16)

qt_base         dw      3F0h
qt_unit         db      0               ; drive select line
qt_id           db      0FFh            ; SCSI ID, 0FFh: no tape drive
qt_cyl          db      0               ; the controller's cylinder
qt_vendor       dw      0
qt_rom          db      0
qt_config       db      0
qt_sns          dd      0               ; sense of the last command
qt_sflags       db      0               ; and its filemark, EOM, ILI
qt_sinfo        dd      0               ; and its information, if not 0FFh..
qt_svalid       db      0
qt_inquiry      db      01h, 80h, 02h, 02h, 31, 0, 0, 0
qt_inq_vendor   db      'QIC-117 '
                db      'FLOPPY TAPE     '
qt_inq_rev      db      '0000'

;-----------------------------------------------------------------------------
; qic_exec: the SCSI command cdb_buf with the request of dev_exec
;-----------------------------------------------------------------------------
qic_exec:
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
        mov     al, [cdb_buf]
        cmp     al, SCSI_REQUEST_SENSE
        je      .rsense
        mov     dword [qt_sns], 0       ; a new command clears the sense
        mov     byte [qt_sflags], 0
        mov     byte [qt_svalid], 0
        cmp     al, SCSI_INQUIRY
        je      .inquiry
        cmp     al, 05h                 ; READ BLOCK LIMITS
        je      .limits

        ; everything else asks the drive first
        call    qt_hook
        call    qt_select
        call    qt_ready
        jc      .sense
        mov     al, [cdb_buf]
        cmp     al, 00h                 ; TEST UNIT READY
        je      .good
        cmp     al, 01h                 ; REWIND
        je      .rewind
        cmp     al, 1Bh                 ; LOAD UNLOAD
        je      .rewind
        cmp     al, 1Ah                 ; MODE SENSE (6)
        je      .msense
        cmp     al, 08h                 ; READ (6)
        je      .read
        cmp     al, 11h                 ; SPACE
        je      .space
        mov     si, qt_nops
.nop:   cmp     byte [si], 0FFh
        je      .bad
        cmp     al, [si]
        je      .good
        inc     si
        jmp     .nop
.bad:   mov     eax, SNS_BAD_OPCODE
        jmp     .sense

.rsense:
        mov     eax, [qt_sns]
        mov     dword [qt_sns], 0
        mov     di, ata_reply
        call    ata_sense_data
        mov     al, [qt_sflags]
        or      [ata_reply+2], al
        cmp     byte [qt_svalid], 0
        je      .rs
        or      byte [ata_reply], 80h   ; the information is valid
        mov     eax, [qt_sinfo]
        BSWAP_EAX
        mov     [ata_reply+3], eax
.rs:    mov     byte [qt_sflags], 0
        mov     byte [qt_svalid], 0
        mov     si, ata_reply
        mov     cx, 18
        call    ata_give
        jmp     .good

.inquiry:
        test    byte [cdb_buf+1], 1     ; vital product data
        jnz     .badfield
        mov     si, qt_inquiry
        mov     cx, 36
        call    ata_give
        jmp     .good

.limits:
        mov     si, qt_limits
        mov     cx, 6
        call    ata_give
        jmp     .good

.rewind:
        call    qt_rewind
        jc      .sense
        mov     byte [qt_vol], 0
        call    qt_vol_start
        jmp     .good

.read:  test    byte [cdb_buf+1], 1     ; fixed blocks: not offered
        jnz     .badfield
        cmp     byte [cdb_buf+2], 0     ; at most 65535 bytes
        jne     .badfield
        call    qt_load
        jc      .sense
        call    qt_read
        jc      .sense
        jmp     .good

.space: call    qt_load
        jc      .sense
        call    qt_space
        jc      .sense
        jmp     .good

.msense:
        push    cs
        pop     es
        mov     di, ata_reply
        xor     ax, ax
        mov     cx, 12 / 2
        rep     stosw
        mov     byte [ata_reply], 11    ; data length
        mov     byte [ata_reply+3], 8   ; one block descriptor
        test    byte [qt_status], QS_WPROT
        jz      .ms
        mov     byte [ata_reply+2], 80h
.ms:    mov     si, ata_reply
        mov     cx, 12
        call    ata_give
        jmp     .good

.badfield:
        mov     eax, SNS_BAD_FIELD
        jmp     .sense
.hw:    mov     eax, SNS_QT_HW
.sense: mov     [qt_sns], eax
        shl     al, 4
        mov     [x_err], al
        mov     byte [x_status], STATUS_CHKCOND
.good:  call    qt_unhook_if
        mov     al, XR_OK
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret

; the BIOS's IRQ 6 handler back, if the driver took it
qt_unhook_if:
        cmp     word [qt_old0e+2], 0
        je      .o
        call    qt_unhook
        mov     word [qt_old0e+2], 0
.o:     ret

qt_nops         db      0Bh, 10h, 15h, 16h, 17h, 1Bh, 1Eh, 0FFh
qt_limits       db      0, 0, 0FFh, 0FFh, 0, 1  ; 1 to 65535 bytes
qt_status       db      0               ; the last drive status

; Is the drive ready?  CF and EAX = the sense if not.  A new cartridge is
; reported once: reading the error code clears it.
qt_ready:
        call    qt_get_status
        jc      .hw
        test    al, QS_ERROR | QS_NEWCART
        jz      .st
        push    ax
        call    qt_get_error
        pop     ax
        jc      .hw
        test    al, QS_NEWCART
        jz      .again
        mov     byte [qt_loaded], 0
        mov     byte [qt_running], 0
        mov     byte [qt_track], 0FFh
        mov     dword [qt_bseg], 0FFFFFFFFh
        mov     eax, SNS_CHANGED
        stc
        ret
.again: call    qt_get_status
        jc      .hw
.st:    test    al, QS_CART
        jz      .none
        test    al, QS_READY
        jnz     .ok
        cmp     byte [qt_running], 0    ; streaming: the drive is busy
        je      .busy
.ok:    clc
        ret
.none:  mov     byte [qt_loaded], 0
        mov     eax, SNS_NO_MEDIUM
        stc
        ret
.busy:  mov     eax, SNS_BECOMING
        stc
        ret
.hw:    mov     eax, SNS_QT_HW
        stc
        ret

; wait until the drive is ready again after a motion command, the command
; timeout.  CF and EAX = the sense if it fails.
qt_ready_wait:
        push    ebx
        push    esi
        call    get_ticks
        mov     esi, eax
.l:     call    qt_get_status
        jc      .hw
        test    al, QS_READY
        jnz     .rdy
        call    get_ticks
        sub     eax, esi
        mov     ebx, [qt_wait]
        or      ebx, ebx
        jnz     .t
        mov     ebx, [tmo_ticks]
.t:     cmp     eax, ebx
        jbe     .l
.hw:    mov     eax, SNS_QT_HW
        stc
        jmp     .o
.rdy:   test    al, QS_ERROR
        jz      .ok
        call    qt_get_error            ; clear it, report the failure
        mov     eax, SNS_QT_HW
        stc
        jmp     .o
.ok:    clc
.o:     pop     esi
        pop     ebx
        ret

;-----------------------------------------------------------------------------
; The volumes
;-----------------------------------------------------------------------------

qt_loaded       db      0               ; header and volume table read
qt_fmt          db      0               ; format code of the header
qt_spt          dw      150             ; segments a track
qt_spc          db      4               ; segments a cylinder
qt_sph          dw      600             ; segments a head
qt_first        dw      0               ; first data segment: the volume table
qt_nbad         dw      0
qt_badlist      times QT_MAXBAD * 6 db 0        ; segment, sector mask
qt_nvol         db      0
qt_vols         times QT_MAXVOL * 8 db 0        ; first, last segment, size
qt_vol          db      0               ; position: volume
qt_voff         dd      0               ; its byte
qt_cseg         dw      0               ; that byte's segment
qt_coff         dw      0               ; and offset in its data
qt_left         dd      0               ; bytes of a READ still to come

qt_buf          dw      0               ; the segment buffer, CS:qt_buf
qt_bufphys      dd      0               ; its physical address (1 KB aligned)
qt_bseg         dd      0FFFFFFFFh      ; the segment it holds
qt_bcap         dw      0               ; its data bytes

qt_running      db      0               ; the tape streams
qt_next         dw      0FFFFh          ; the segment it is about to pass
qt_track        db      0FFh            ; the head's track, 0FFh: unknown
qt_wait         dd      0               ; ticks for qt_ready_wait, 0: /T:
qt_target       dw      0
qt_ttrack       db      0
qt_tindex       dw      0
qt_rseg         dw      0
qt_tries        db      0
qt_rmask        dd      0
qt_res          times 7 db 0            ; result bytes of a read or READ ID
qt_idsec        db      0               ; READ ID: sector in the segment

QT_WIND_TICKS   equ     3640            ; a whole track: 200 s

; the position at the start of volume [qt_vol]
qt_vol_start:
        push    ax
        push    bx
        mov     dword [qt_voff], 0
        mov     word [qt_coff], 0
        movzx   bx, byte [qt_vol]
        cmp     bl, [qt_nvol]
        jae     .o
        shl     bx, 3
        mov     ax, [qt_vols+bx]
        mov     [qt_cseg], ax
.o:     pop     bx
        pop     ax
        ret

; the size of volume [qt_vol] -> EAX (0 past the last)
qt_vol_size:
        push    bx
        xor     eax, eax
        movzx   bx, byte [qt_vol]
        cmp     bl, [qt_nvol]
        jae     .o
        shl     bx, 3
        mov     eax, [qt_vols+bx+4]
.o:     pop     bx
        ret

; READ (6) of variable blocks: the bytes of the CDB, at most the buffer.
; A volume's end is a filemark: what is left is returned, then CHECK
; CONDITION, FILEMARK, the residue in the information.  CF and EAX = the
; sense on failure.
qt_read:
        movzx   eax, word [cdb_buf+3]
        xchg    al, ah
        cmp     eax, [x_len]
        jbe     .len
        mov     eax, [x_len]
        mov     byte [x_over], 1
.len:   mov     [qt_left], eax
        or      eax, eax
        jz      .ok
        mov     al, [qt_vol]
        cmp     al, [qt_nvol]
        jb      .vol
        mov     eax, [qt_left]          ; past the last volume: end of data
        mov     [qt_sinfo], eax
        mov     byte [qt_svalid], 1
        mov     byte [qt_sflags], QT_FL_EOM
        mov     eax, SNS_QT_EOD
        stc
        ret
.vol:   call    qt_vol_size
        sub     eax, [qt_voff]          ; left in the volume
        mov     ecx, [qt_left]
        cmp     ecx, eax
        jbe     .n
        mov     ecx, eax
.n:     push    ecx
        call    qt_copy_out
        pop     ecx
        jc      .o
        sub     [qt_left], ecx
        jz      .ok
        inc     byte [qt_vol]           ; the filemark: on to the next volume
        call    qt_vol_start
        mov     eax, [qt_left]
        mov     [qt_sinfo], eax
        mov     byte [qt_svalid], 1
        mov     byte [qt_sflags], QT_FL_FM | QT_FL_ILI
        mov     eax, SNS_QT_FM
        stc
        ret
.ok:    clc
.o:     ret

; copy ECX bytes of the volume from the position to the request buffer.
; CF and EAX = the sense on failure.
qt_copy_out:
        push    ebx
        push    ecx
        push    si
        push    di
        push    es
.next:  or      ecx, ecx
        jz      .ok
        movzx   eax, word [qt_cseg]
        cmp     eax, [qt_bseg]
        je      .have
        call    qt_read_seg
        jc      .o
.have:  mov     ax, [qt_bcap]
        sub     ax, [qt_coff]
        ja      .take
        inc     word [qt_cseg]          ; on to the next segment
        mov     word [qt_coff], 0
        jmp     .next
.take:  movzx   ebx, ax                 ; this chunk: what the segment has
        cmp     ebx, ecx
        jbe     .c
        mov     ebx, ecx
.c:     push    ecx
        mov     ecx, ebx
        mov     si, [qt_buf]
        add     si, [qt_coff]
        les     di, [x_buf]
        rep     movsb
        call    norm_buf                ; ES:DI on: keep the offset small
        pop     ecx
        add     [x_xfer], ebx
        add     [qt_voff], ebx
        add     [qt_coff], bx
        sub     ecx, ebx
        jmp     .next
.ok:    clc
.o:     pop     es
        pop     di
        pop     si
        pop     ecx
        pop     ebx
        ret

; SPACE: filemarks (code 1), the end of data (code 3).  CF and EAX = the
; sense if the tape ends first.
qt_space:
        mov     al, [cdb_buf+1]
        and     al, 7
        cmp     al, 3
        je      .eod
        cmp     al, 1
        jne     .bad
        mov     eax, [cdb_buf+1]        ; the count: 24 bits, signed
        BSWAP_EAX
        shl     eax, 8
        sar     eax, 8
        or      eax, eax
        jz      .ok
        js      .back
        movzx   ebx, byte [qt_vol]
        add     ebx, eax
        movzx   eax, byte [qt_nvol]
        cmp     ebx, eax
        ja      .pasteod
        mov     [qt_vol], bl
        call    qt_vol_start
.ok:    clc
        ret
.eod:   mov     al, [qt_nvol]
        mov     [qt_vol], al
        call    qt_vol_start
        clc
        ret
.pasteod:
        sub     ebx, eax                ; the filemarks not spaced over
        mov     [qt_sinfo], ebx
        mov     byte [qt_svalid], 1
        mov     byte [qt_sflags], QT_FL_EOM
        mov     al, [qt_nvol]
        mov     [qt_vol], al
        call    qt_vol_start
        mov     eax, SNS_QT_EOD
        stc
        ret
.back:  neg     eax                     ; to the end of the volume before
        movzx   ebx, byte [qt_vol]      ; the n-th filemark back
        sub     ebx, eax
        js      .bot
        mov     [qt_vol], bl
        call    qt_vol_start
        call    qt_vol_size
        mov     [qt_voff], eax
        mov     bx, [qt_vols+ebx*8+2]   ; past its last segment
        inc     bx
        mov     [qt_cseg], bx
        clc
        ret
.bot:   neg     ebx
        mov     [qt_sinfo], ebx
        mov     byte [qt_svalid], 1
        mov     byte [qt_sflags], QT_FL_EOM
        mov     byte [qt_vol], 0
        call    qt_vol_start
        mov     eax, SNS_QT_BOT
        stc
        ret
.bad:   mov     eax, SNS_BAD_FIELD
        stc
        ret

; to the load point, the head on track 0.  CF and EAX = the sense.
qt_rewind:
        call    qt_stop
        mov     al, QIC_LOAD_POINT
        call    qt_command
        jc      .hw
        mov     dword [qt_wait], QT_WIND_TICKS
        call    qt_ready_wait
        mov     dword [qt_wait], 0
        jc      .o
        mov     byte [qt_track], 0
        mov     word [qt_next], 0FFFFh
        clc
.o:     ret
.hw:    mov     eax, SNS_QT_HW
        stc
        ret

; stop the tape if it streams.  CF and EAX = the sense.
qt_stop:
        cmp     byte [qt_running], 0
        je      .ok
        mov     byte [qt_running], 0
        mov     word [qt_next], 0FFFFh
        mov     al, QIC_STOP
        call    qt_command
        jc      .hw
        jmp     qt_ready_wait
.ok:    clc
        ret
.hw:    mov     eax, SNS_QT_HW
        stc
        ret

;-----------------------------------------------------------------------------
; The header segment and the volume table
;-----------------------------------------------------------------------------

; read them once a cartridge is in.  CF and EAX = the sense.
qt_load:
        cmp     byte [qt_loaded], 0
        jne     .ok
        call    qt_rewind
        jc      .o
        mov     word [qt_nbad], 0       ; the header has no bad sectors
        mov     word [qt_spt], 150      ; the QIC-80 grid until it is read
        mov     byte [qt_spc], 4
        mov     word [qt_sph], 600
        xor     eax, eax                ; the header: the first segment
.hs:    push    eax                     ; with the signature
        call    qt_read_seg
        jc      .hnext
        mov     si, [qt_buf]
        cmp     dword [si], 0AA55AA55h
        je      .found
.hnext: pop     eax
        inc     eax
        cmp     eax, 16
        jb      .hs
        mov     eax, SNS_QT_FORMAT
        stc
        ret
.found: pop     eax
        call    qt_header
        jc      .o
        movzx   eax, word [qt_first]    ; the volume table
        call    qt_read_seg
        jc      .o
        call    qt_vtbl
        mov     byte [qt_loaded], 1
        mov     byte [qt_vol], 0
        call    qt_vol_start
.ok:    clc
.o:     ret

; the header segment in the buffer: the geometry and the bad sector map.
; CF and EAX = the sense if the format is not known.
qt_header:
        mov     si, [qt_buf]
        mov     al, [si+4]
        mov     [qt_fmt], al
        cmp     al, 2
        jb      .bad
        cmp     al, 6
        ja      .bad
        mov     ax, [si+10]             ; first data segment
        cmp     byte [qt_fmt], 6
        jne     .f
        mov     ax, [si+242]
.f:     mov     [qt_first], ax
        mov     ax, [si+24]
        or      ax, ax
        jz      .bad
        mov     [qt_spt], ax
        mov     al, [si+29]             ; max floppy sector
        shr     al, 5
        jz      .bad
        mov     [qt_spc], al
        movzx   ax, byte [si+28]        ; max floppy track
        inc     ax
        mul     byte [qt_spc]
        mov     [qt_sph], ax

        mov     word [qt_nbad], 0
        mov     al, [qt_fmt]
        cmp     al, 3                   ; 1100 ft: a sector list at 2048
        je      .list2048
        cmp     al, 4                   ; QIC-3010/3020 and wide QIC-80:
        je      .list256                ; a sector list at 256
        cmp     al, 6
        je      .list256

        ; a sector mask a segment from offset 2048
        add     si, 2048
        xor     dx, dx                  ; segment
.m:     mov     eax, [si]
        or      eax, eax
        jz      .mn
        call    qt_bad_add
.mn:    add     si, 4
        inc     dx
        mov     ax, si
        sub     ax, [qt_buf]
        cmp     ax, 29 * 1024
        jb      .m
        clc
        ret

.list2048:
        add     si, 2048
        jmp     .list
.list256:
        add     si, 256
.list:  mov     ax, si
        sub     ax, [qt_buf]
        cmp     ax, 29 * 1024 - 3
        ja      .ldone
        mov     eax, [si]
        and     eax, 0FFFFFFh
        jz      .ldone
        add     si, 3
        mov     ecx, eax
        and     eax, 7FFFFFh            ; the sector, from 1
        dec     eax
        xor     edx, edx
        mov     ebx, 32
        div     ebx                     ; EAX = segment, EDX = sector
        cmp     eax, 0FFFFh
        ja      .list
        xchg    eax, edx                ; DX = segment
        mov     cl, al
        mov     eax, 1
        shl     eax, cl
        test    ecx, 800000h            ; the whole segment, format 4 and 6
        jz      .one
        mov     eax, 0FFFFFFFFh
.one:   call    qt_bad_add
        jmp     .list
.ldone: clc
        ret
.bad:   mov     eax, SNS_QT_FORMAT
        stc
        ret

; segment DX has the bad sectors EAX: into the list (sorted as the map)
qt_bad_add:
        push    bx
        mov     bx, [qt_nbad]
        or      bx, bx
        jz      .new
        imul    bx, bx, 6
        cmp     [qt_badlist+bx-6], dx   ; the last entry's segment?
        jne     .new2
        or      [qt_badlist+bx-6+2], eax
        jmp     .o
.new:   xor     bx, bx
.new2:  cmp     word [qt_nbad], QT_MAXBAD
        jae     .o                      ; too many: read as good
        mov     [qt_badlist+bx], dx
        mov     [qt_badlist+bx+2], eax
        inc     word [qt_nbad]
.o:     pop     bx
        ret

; the bad sectors of segment AX -> EAX
qt_badmask:
        push    bx
        push    cx
        mov     cx, [qt_nbad]
        mov     bx, qt_badlist
        jcxz    .none
.l:     cmp     [bx], ax
        je      .f
        add     bx, 6
        loop    .l
.none:  xor     eax, eax
        jmp     .o
.f:     mov     eax, [bx+2]
.o:     pop     cx
        pop     bx
        ret

; the data bytes of segment AX -> AX (0 with three good sectors or fewer)
qt_capacity:
        push    ecx
        push    dx
        call    qt_badmask
        not     eax
        xor     cx, cx                  ; good sectors
.cnt:   shr     eax, 1
        adc     cx, 0
        or      eax, eax
        jnz     .cnt
        xor     ax, ax
        sub     cx, 3
        jbe     .o
        mov     ax, cx
        shl     ax, 10
.o:     pop     dx
        pop     ecx
        ret

; the volume table in the buffer -> qt_vols
qt_vtbl:
        mov     byte [qt_nvol], 0
        mov     si, [qt_buf]
        mov     cx, [qt_bcap]
        shr     cx, 7                   ; 128 byte entries
        or      cx, cx
        jz      .o
        mov     di, qt_vols
.e:     mov     eax, [si]
        cmp     eax, 'VTBL'
        je      .vol
        cmp     eax, 'XTBL'             ; other entries of the table
        je      .skip
        cmp     eax, 'UTID'
        je      .skip
        cmp     eax, 'EXVT'
        jne     .o
.skip:  add     si, 128
        dec     cx
        jnz     .e
        jmp     .o
.vol:   cmp     byte [qt_nvol], QT_MAXVOL
        jae     .o
        mov     ax, [si+4]              ; first and last segment
        mov     bx, [si+6]
        cmp     byte [qt_fmt], 6
        jne     .se
        mov     ax, [si+76]
        mov     bx, [si+80]
.se:    mov     [di], ax
        mov     [di+2], bx
        mov     eax, [si+96]            ; the size of the data
        cmp     dword [si+100], 0
        je      .sz
        mov     eax, 0FFFFFFFFh
.sz:    or      eax, eax
        jnz     .put
        push    dx                      ; none: all its segments hold
        mov     dx, [di]
.sum:   cmp     dx, [di+2]
        ja      .sumd
        push    eax
        mov     ax, dx
        call    qt_capacity
        movzx   edx, dx
        movzx   eax, ax
        pop     ebx
        add     eax, ebx
        inc     dx
        jmp     .sum
.sumd:  pop     dx
.put:   mov     [di+4], eax
        add     di, 8
        inc     byte [qt_nvol]
        add     si, 128
        dec     cx
        jnz     .e
.o:     ret

;-----------------------------------------------------------------------------
; Segments
;-----------------------------------------------------------------------------

; read segment EAX into the buffer: its good sectors, one after the other.
; CF and EAX = the sense on failure.
qt_read_seg:
        push    ebx
        push    ecx
        push    edx
        push    si
        push    di
        mov     dword [qt_bseg], 0FFFFFFFFh
        mov     [qt_rseg], ax
        call    qt_badmask
        mov     [qt_rmask], eax
        mov     ax, [qt_rseg]
        call    qt_capacity
        mov     [qt_bcap], ax
        or      ax, ax
        jnz     .read
        mov     ax, [qt_rseg]           ; no data in it: nothing to read
        cmp     [qt_next], ax
        jne     .done
        inc     word [qt_next]
        jmp     .done
.read:  mov     byte [qt_tries], 3
.again: mov     ax, [qt_rseg]
        call    qt_locate
        jc      .o
        mov     di, [qt_buf]
        xor     bl, bl                  ; sector
.run:   cmp     bl, 32
        jae     .read_ok
        movzx   ecx, bl
        bt      [qt_rmask], ecx
        jnc     .start
        inc     bl
        jmp     .run
.start: xor     bh, bh                  ; a run of good sectors
.ext:   mov     cl, bl
        add     cl, bh
        cmp     cl, 32
        jae     .go
        movzx   ecx, cl
        bt      [qt_rmask], ecx
        jc      .go
        or      bh, bh                  ; not across a 64 KB DMA boundary
        jz      .grow
        mov     ax, di
        sub     ax, [qt_buf]
        movzx   eax, ax
        movzx   edx, bh
        shl     edx, 10
        add     eax, edx
        add     eax, [qt_bufphys]
        test    ax, ax
        jz      .go
.grow:  inc     bh
        jmp     .ext
.go:    call    qt_run
        jc      .err
        movzx   ax, bh
        shl     ax, 10
        add     di, ax
        add     bl, bh
        jmp     .run
.read_ok:
        call    qt_ecc                  ; a damaged sector is put right
        jc      .rderr
        mov     ax, [qt_rseg]           ; the tape streams on
        inc     ax
        mov     [qt_next], ax
        xor     dx, dx
        div     word [qt_spt]
        or      dx, dx                  ; but stops at the end of a track
        jnz     .done
        mov     word [qt_next], 0FFFFh
.done:  movzx   eax, word [qt_rseg]
        mov     [qt_bseg], eax
        clc
        jmp     .o
.err:   cmp     al, 1                   ; not found: find the segment again
        jne     .rderr
        call    qt_stop
        dec     byte [qt_tries]
        jnz     .again
.rderr: call    qt_stop
        mov     eax, SNS_QT_READ
        stc
.o:     pop     di
        pop     si
        pop     edx
        pop     ecx
        pop     ebx
        ret

;-----------------------------------------------------------------------------
; The Reed-Solomon code of a segment
;
; Each byte column of a segment's n good sectors is a code word over
; GF(256) (x^8+x^7+x^2+x+1) whose generator has the roots a^-1, 1 and a:
; good sector i is the coefficient of x^(n-1-i), the last three carry the
; parity.  The syndromes S-1, S0 and S1 of a whole column are zero.  One
; damaged sector, the one the controller read without a CRC error but
; wrong, gives in each column it touches the error e = S0 at X = S1 / S0
; = a^(n-1-i), with S-1 = S0 / X; all columns must agree on i.  More
; damage than that, or sectors the controller could not read at all, is
; not corrected yet.
;-----------------------------------------------------------------------------

gf_log          times 256 db 0
qt_epos         dw      0               ; the damaged sector, FFFFh: none yet

qt_gf_init:
        push    ax
        push    bx
        push    cx
        xor     cl, cl                  ; i
        mov     al, 1                   ; a^i
.l:     movzx   bx, al
        mov     [gf_log+bx], cl
        shl     al, 1
        jnc     .n
        xor     al, 87h
.n:     inc     cl
        cmp     cl, 255
        jb      .l
        pop     cx
        pop     bx
        pop     ax
        ret

; check, and correct, the segment in the buffer: [qt_bcap] / 1024 + 3
; good sectors.  CF if it is damaged beyond that.
qt_ecc: pushad
        mov     bp, [qt_bcap]
        shr     bp, 10
        add     bp, 3                   ; n
        xor     di, di                  ; quick: S0 of every column
.q:     mov     si, [qt_buf]
        add     si, di
        mov     cx, bp
        xor     eax, eax
.qs:    xor     eax, [si]
        add     si, 1024
        loop    .qs
        or      eax, eax
        jnz     .full
        add     di, 4
        cmp     di, 1024
        jb      .q
        jmp     .ok
.full:  mov     word [qt_epos], 0FFFFh
        xor     di, di                  ; column
.col:   mov     si, [qt_buf]
        add     si, di
        mov     cx, bp
        xor     dx, dx                  ; DL = S0, DH = S1
        xor     bl, bl                  ; BL = S-1
.s:     mov     al, [si]
        xor     dl, al
        shl     dh, 1                   ; S1 * a
        jnc     .a
        xor     dh, 87h
.a:     xor     dh, al
        shr     bl, 1                   ; S-1 * a^-1
        jnc     .b
        xor     bl, 0C3h
.b:     xor     bl, al
        add     si, 1024
        loop    .s
        or      dl, dl
        jnz     .err
        or      dh, bl                  ; S0 zero: the others too
        jnz     .bad
        jmp     .next
.err:   or      dh, dh
        jz      .bad
        or      bl, bl
        jz      .bad
        movzx   si, dl
        movzx   ax, byte [gf_log+si]    ; log S0
        movzx   si, dh
        movzx   cx, byte [gf_log+si]    ; log X = log S1 - log S0
        sub     cx, ax
        jae     .x
        add     cx, 255
.x:     movzx   si, bl                  ; log S-1 + log X = log S0
        movzx   si, byte [gf_log+si]
        add     si, cx
        cmp     si, 255
        jb      .y
        sub     si, 255
.y:     cmp     si, ax
        jne     .bad
        mov     ax, bp                  ; i = n - 1 - log X
        dec     ax
        sub     ax, cx
        jb      .bad
        cmp     word [qt_epos], 0FFFFh
        je      .first
        cmp     [qt_epos], ax
        jne     .bad
.first: mov     [qt_epos], ax
        shl     ax, 10                  ; e into the damaged byte
        add     ax, di
        add     ax, [qt_buf]
        mov     si, ax
        xor     [si], dl
.next:  inc     di
        cmp     di, 1024
        jb      .col
.ok:    popad
        clc
        ret
.bad:   popad
        stc
        ret

; get the tape streaming just before segment AX.  CF and EAX = the sense.
qt_locate:
        push    bx
        push    cx
        push    dx
        push    si
        mov     [qt_target], ax
        cmp     byte [qt_running], 0
        je      .stopped
        cmp     ax, [qt_next]           ; it is coming
        je      .ok
        call    qt_stop
        jc      .o
.stopped:
        mov     ax, [qt_target]
        xor     dx, dx
        div     word [qt_spt]           ; AX = track, DX = segment in it
        mov     [qt_tindex], dx
        cmp     al, [qt_track]
        je      .ontrack
        mov     [qt_ttrack], al
        mov     al, QIC_TRACK
        call    qt_command
        jc      .hw
        mov     al, [qt_ttrack]
        add     al, 2                   ; a parameter
        call    qt_command
        jc      .hw
        call    qt_ready_wait
        jc      .o
        mov     al, [qt_ttrack]
        mov     [qt_track], al
.ontrack:
        cmp     word [qt_tindex], 0
        jne     .find
        mov     al, QIC_PHYS_REV        ; the first segment of a track: from
        test    byte [qt_track], 1      ; where the track begins (BOT for the
        jz      .wind                   ; even tracks, EOT for the odd ones)
        mov     al, QIC_PHYS_FWD
.wind:  call    qt_command
        jc      .hw
        mov     dword [qt_wait], QT_WIND_TICKS
        call    qt_ready_wait
        mov     dword [qt_wait], 0
        jc      .o
        call    qt_forward
        jc      .o
        jmp     .ok

.find:  mov     cx, QT_TRIES
.try:   cmp     byte [qt_running], 0
        jne     .id0
        call    qt_forward
        jc      .o
.id0:   mov     si, 4 * 32 + 8          ; IDs to read before skipping on
.id:    call    qt_readid
        jc      .idfail
        mov     bx, [qt_target]
        dec     bx                      ; the segment before the target
        cmp     ax, bx
        je      .ok
        ja      .back
        mov     dx, bx                  ; still before it
        sub     dx, ax
        cmp     dx, 4
        ja      .fwd
        dec     si
        jnz     .id
.fwd:   call    qt_stop                 ; skip on to it
        jc      .o
        dec     dx
        jz      .retry
        mov     al, QIC_SKIP_FWD
        call    qt_skip
        jc      .o
        jmp     .retry
.back:  call    qt_stop                 ; past it: back
        jc      .o
        mov     dx, ax
        sub     dx, bx
        inc     dx
        mov     al, QIC_SKIP_REV
        call    qt_skip
        jc      .o
        jmp     .retry
.idfail:
        call    qt_stop
        jc      .o
.retry: loop    .try
        mov     eax, SNS_QT_READ
        stc
        jmp     .o
.ok:    mov     ax, [qt_target]
        mov     [qt_next], ax
        clc
.o:     pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
.hw:    mov     eax, SNS_QT_HW
        stc
        jmp     .o

; start streaming.  CF and EAX = the sense.
qt_forward:
        mov     al, QIC_FORWARD
        call    qt_command
        jc      .hw
        mov     byte [qt_running], 1
        mov     word [qt_next], 0FFFFh
        clc
        ret
.hw:    mov     eax, SNS_QT_HW
        stc
        ret

; skip DX segments, AL = QIC_SKIP_FWD or QIC_SKIP_REV.  CF and EAX = the
; sense.
qt_skip:
        push    cx
        push    dx
        dec     dx                      ; 0: one gap
        mov     cx, 2                   ; nibbles
        cmp     dx, 0FFh
        jbe     .c
        add     al, QIC_SKIPX_FWD - QIC_SKIP_FWD
        inc     cx
.c:     call    qt_command
        jc      .hw
.p:     mov     al, dl
        and     al, 0Fh
        add     al, 2                   ; a parameter
        call    qt_command
        jc      .hw
        shr     dx, 4
        loop    .p
        mov     dword [qt_wait], QT_WIND_TICKS
        call    qt_ready_wait
        mov     dword [qt_wait], 0
        jmp     .o
.hw:    mov     eax, SNS_QT_HW
        stc
.o:     pop     dx
        pop     cx
        ret

; READ ID -> AX = the segment under the head (qt_idsec its sector).  CF if
; there is none.
qt_readid:
        push    bx
        push    dx
        call    qt_select
        call    qt_int_clear
        mov     al, 4Ah                 ; READ ID, MFM
        call    qt_out
        jc      .o
        mov     al, [qt_unit]
        call    qt_out
        jc      .o
        call    qt_result
        jc      .o
        mov     al, [qt_res+5]          ; R
        dec     al
        mov     ah, al
        and     ah, 1Fh
        mov     [qt_idsec], ah
        shr     al, 5
        movzx   bx, al
        movzx   ax, byte [qt_res+3]     ; C
        mul     byte [qt_spc]
        add     bx, ax
        movzx   ax, byte [qt_res+4]     ; H
        mul     word [qt_sph]
        add     ax, bx
        clc
.o:     pop     dx
        pop     bx
        ret

; wait for the interrupt and the seven result bytes of a read or READ ID.
; CF if it fails (AL = 1: the sector was not found).
qt_result:
        push    ebx
        push    cx
        push    di
        mov     ebx, QT_RW_TICKS
        call    qt_wait_int
        jc      .reset
        mov     di, qt_res
        mov     cx, 7
.r:     call    qt_in
        jc      .reset
        mov     [di], al
        inc     di
        loop    .r
        test    byte [qt_res], 0C0h     ; ST0: normal termination
        jz      .o
        mov     al, 1
        test    byte [qt_res+1], 05h    ; ST1: no data, missing address mark
        jnz     .f
        mov     al, 2                   ; a data error, or else
.f:     stc
        jmp     .o
.reset: call    qt_reset                ; the controller is stuck
        mov     al, 3
        stc
.o:     pop     di
        pop     cx
        pop     ebx
        ret

; read BH sectors of segment [qt_rseg] from sector BL to CS:DI.  CF on
; failure, AL = 1 if the sectors were not found.
qt_run:
        push    bx
        push    cx
        push    dx
        movzx   cx, bh
        shl     cx, 10
        call    qt_dma
        mov     ax, [qt_rseg]
        xor     dx, dx
        div     word [qt_sph]           ; AX = head, DX = segment in it
        mov     ch, al
        mov     ax, dx
        div     byte [qt_spc]           ; AL = cylinder, AH = segment in it
        mov     cl, al
        mov     al, ah
        shl     al, 5
        inc     al
        add     al, bl                  ; the first sector
        mov     dl, al
        call    qt_select
        call    qt_int_clear
        mov     al, 46h                 ; READ DATA, MFM
        call    qt_out
        jc      .f
        mov     al, [qt_unit]
        call    qt_out
        jc      .f
        mov     al, cl                  ; C
        call    qt_out
        jc      .f
        mov     al, ch                  ; H
        call    qt_out
        jc      .f
        mov     al, dl                  ; R
        call    qt_out
        jc      .f
        mov     al, 3                   ; 1024 bytes
        call    qt_out
        jc      .f
        mov     al, dl                  ; the last sector
        add     al, bh
        dec     al
        call    qt_out
        jc      .f
        mov     al, 109                 ; gap length
        call    qt_out
        jc      .f
        mov     al, 0FFh
        call    qt_out
        jc      .f
        call    qt_result
        jmp     .o
.f:     mov     al, 3
.o:     pop     dx
        pop     cx
        pop     bx
        ret

; DMA channel 2, controller to memory: CX bytes to CS:DI
qt_dma:
        push    eax
        mov     ax, di
        sub     ax, [qt_buf]
        movzx   eax, ax
        add     eax, [qt_bufphys]
        pushf
        cli
        push    eax
        mov     al, 06h                 ; mask channel 2
        out     0Ah, al
        out     0Ch, al                 ; the flip-flop
        mov     al, 46h                 ; single, increment, write memory
        out     0Bh, al
        pop     eax
        out     04h, al
        mov     al, ah
        out     04h, al
        shr     eax, 16
        out     81h, al                 ; the page
        mov     ax, cx
        dec     ax
        out     05h, al
        mov     al, ah
        out     05h, al
        mov     al, 02h                 ; unmask
        out     0Ah, al
        popf
        pop     eax
        ret

;-----------------------------------------------------------------------------
; QIC-117
;-----------------------------------------------------------------------------

; the drive status -> AL (and qt_status).  CF if the drive does not answer.
qt_get_status:
        push    cx
        mov     al, QIC_STATUS
        mov     cl, 8
        call    qt_report
        pop     cx
        jc      .o
        mov     [qt_status], al
.o:     ret

; the error code -> AL, the command that caused it -> AH.  Clears the error
; and a new cartridge.
qt_get_error:
        push    cx
        mov     al, QIC_ERROR
        mov     cl, 16
        call    qt_report
        pop     cx
        ret

; report command AL with CL bits of result -> EAX.  CF on failure.
qt_report:
        push    ebx
        push    cx
        push    dx
        call    qt_command
        jc      .fail
        mov     ch, QT_ACK_TRIES        ; the acknowledge: TRACK 0 set
.ack:   push    cx
        mov     cx, QT_GAP
        call    delay15
        pop     cx
        call    qt_st3
        jc      .fail
        test    al, 10h
        jnz     .bits
        dec     ch
        jnz     .ack
        jmp     .fail
.bits:  xor     ebx, ebx
        xor     ch, ch                  ; bit number
.bit:   mov     al, QIC_NEXT_BIT
        call    qt_command
        jc      .fail
        call    qt_st3
        jc      .fail
        cmp     ch, cl
        je      .stop
        test    al, 10h
        jz      .zero
        movzx   edx, ch
        bts     ebx, edx
.zero:  inc     ch
        jmp     .bit
.stop:  test    al, 10h                 ; the stop bit is a one
        jz      .fail
        mov     al, QIC_NEXT_BIT        ; TRACK 0 back to normal
        call    qt_command
        mov     eax, ebx
        clc
        jmp     .o
.fail:  stc
.o:     pop     dx
        pop     cx
        pop     ebx
        ret

; send command (or parameter + 2) AL to the drive: that many step pulses,
; towards cylinder 0 where the controller's cylinder allows.  CF on failure.
qt_command:
        push    cx
        push    ax
        mov     cx, QT_GAP
        call    delay15
        pop     ax
        mov     ah, [qt_cyl]
        cmp     ah, al
        jb      .up
        sub     ah, al
        jmp     .seek
.up:    add     ah, al
.seek:  mov     al, ah
        pop     cx
        jmp     qt_seek

; the drive's select line, without a motor, before every command: the
; BIOS's timer tick turns the floppy motors off when their time is up and
; selects drive 0 with it.  Its motors are off now, and stay so.
qt_select:
        push    ax
        push    dx
        push    ds
        push    word 40h
        pop     ds
        mov     byte [40h], 0           ; the motor time-out
        and     byte [3Fh], 0F0h        ; the motors that run
        pop     ds
        mov     dx, [qt_base]
        add     dx, QT_DOR
        mov     al, [qt_unit]
        or      al, QT_DOR_RUN
        out     dx, al
        pop     dx
        pop     ax
        ret

;-----------------------------------------------------------------------------
; The floppy controller
;-----------------------------------------------------------------------------

; AL = a command or parameter byte.  CF on timeout.
qt_out: push    eax
        push    bx
        push    dx
        push    esi
        mov     bl, al
        call    get_ticks
        mov     esi, eax
        mov     dx, [qt_base]
        add     dx, QT_MSR
.l:     in      al, dx
        and     al, 0C0h
        cmp     al, 80h                 ; RQM, to the controller
        je      .w
        call    get_ticks
        sub     eax, esi
        cmp     eax, CMD_TICKS
        jbe     .l
        stc
        jmp     .o
.w:     inc     dx                      ; the data register
        mov     al, bl
        out     dx, al
        clc
.o:     pop     esi
        pop     dx
        pop     bx
        pop     eax
        ret

; -> AL = a result byte.  CF on timeout.
qt_in:  push    dx
        push    esi
        push    ebx
        mov     bh, ah                  ; AH is kept
        call    get_ticks
        mov     esi, eax
        mov     dx, [qt_base]
        add     dx, QT_MSR
.l:     in      al, dx
        and     al, 0C0h
        cmp     al, 0C0h                ; RQM, from the controller
        je      .r
        call    get_ticks
        sub     eax, esi
        cmp     eax, CMD_TICKS
        jbe     .l
        stc
        jmp     .o
.r:     inc     dx
        in      al, dx
        clc
.o:     mov     ah, bh
        pop     ebx
        pop     esi
        pop     dx
        ret

; SENSE DRIVE STATUS -> AL = ST3.  CF on timeout.
qt_st3: call    qt_select
        mov     al, 04h
        call    qt_out
        jc      .o
        mov     al, [qt_unit]
        call    qt_out
        jc      .o
        call    qt_in
.o:     ret

; While the driver works the controller, IRQ 6 is its own: some BIOS
; handlers take the interrupt status themselves (SENSE INTERRUPT STATUS),
; and a seek's result would be lost.
qt_hook:
        push    ax
        push    bx
        push    es
        xor     ax, ax
        mov     es, ax
        mov     byte [qt_irq], 0
        cli
        mov     ax, [es:0Eh*4]
        mov     bx, [es:0Eh*4+2]
        mov     [qt_old0e], ax
        mov     [qt_old0e+2], bx
        mov     word [es:0Eh*4], qt_isr
        mov     [es:0Eh*4+2], cs
        sti
        pop     es
        pop     bx
        pop     ax
        ret

qt_unhook:
        push    ax
        push    bx
        push    es
        xor     ax, ax
        mov     es, ax
        mov     ax, [qt_old0e]
        mov     bx, [qt_old0e+2]
        cli
        mov     [es:0Eh*4], ax
        mov     [es:0Eh*4+2], bx
        sti
        pop     es
        pop     bx
        pop     ax
        ret

qt_isr: mov     byte [cs:qt_irq], 1
        push    ax
        mov     al, 20h
        out     20h, al
        pop     ax
        iret

qt_old0e        dd      0
qt_irq          db      0

qt_int_clear:
        mov     byte [qt_irq], 0
        ret

; wait for the controller's interrupt, EBX ticks.  CF on timeout.
qt_wait_int:
        push    eax
        push    esi
        pushf
        sti
        call    get_ticks
        mov     esi, eax
.l:     cmp     byte [qt_irq], 0
        jne     .ok
        call    get_ticks
        sub     eax, esi
        cmp     eax, ebx
        jbe     .l
        popf
        stc
        jmp     .o
.ok:    mov     byte [qt_irq], 0
        popf
        clc
.o:     pop     esi
        pop     eax
        ret

; SENSE INTERRUPT STATUS -> AL = ST0, AH = cylinder.  CF on timeout.
qt_sense_int:
        mov     al, 08h
        call    qt_out
        jc      .o
        call    qt_in
        jc      .o
        mov     ah, al
        cmp     al, 80h                 ; no interrupt: no cylinder
        je      .o
        call    qt_in
        jc      .o
        xchg    al, ah
.o:     ret

; SEEK to cylinder AL.  CF on failure.
qt_seek:
        push    ebx
        push    cx
        mov     cl, al
        call    qt_select
        call    qt_int_clear
        mov     al, 0Fh
        call    qt_out
        jc      .o
        mov     al, [qt_unit]
        call    qt_out
        jc      .o
        mov     al, cl
        call    qt_out
        jc      .o
        mov     ebx, QT_INT_TICKS
        call    qt_wait_int
        jc      .o
        call    qt_sense_int
        jc      .o
        mov     [qt_cyl], ah
        test    al, 20h                 ; seek end
        jz      .fail
        cmp     ah, cl
        je      .o
.fail:  stc
.o:     pop     cx
        pop     ebx
        ret

; reset the controller: select the drive, 500 kbit/s, 2 ms steps.  CF if
; there is no controller.
qt_reset:
        push    ebx
        push    cx
        push    dx
        call    qt_int_clear
        mov     dx, [qt_base]
        add     dx, QT_DOR
        mov     al, [qt_unit]
        out     dx, al                  ; reset
        mov     cx, 2
        call    delay15
        or      al, QT_DOR_RUN
        out     dx, al
        mov     ebx, QT_INT_TICKS
        call    qt_wait_int
        jc      .o
        mov     cx, 4                   ; one for each drive
.si:    call    qt_sense_int
        jc      .o
        loop    .si
        mov     al, 03h                 ; SPECIFY
        call    qt_out
        jc      .o
        mov     al, 0E1h                ; 2 ms steps at 500 kbit/s, unload
        call    qt_out
        jc      .o
        mov     al, 02h                 ; head load, DMA
        call    qt_out
        jc      .o
        mov     dx, [qt_base]
        add     dx, QT_CCR
        xor     al, al                  ; 500 kbit/s
        out     dx, al
        mov     byte [qt_cyl], 0
        push    ds                      ; the BIOS must recalibrate its drives
        push    word 40h
        pop     ds
        and     byte [3Eh], 70h
        pop     ds
        clc
.o:     pop     dx
        pop     cx
        pop     ebx
        ret

;-----------------------------------------------------------------------------
; qt_buffer: the segment buffer after the resident part (offset AX), 1 KB
; aligned so that a 64 KB DMA boundary falls between sectors.  -> AX = the
; new end of the resident part.  CF if it does not fit the segment.
;-----------------------------------------------------------------------------
qt_buffer:
        push    ebx
        push    edx
        movzx   eax, ax
        mov     bx, cs
        movzx   ebx, bx
        shl     ebx, 4
        add     eax, ebx                ; physical
        add     eax, 1023
        and     eax, 0FFFFFC00h
        mov     [qt_bufphys], eax
        sub     eax, ebx
        mov     [qt_buf], ax
        add     eax, QT_BUFSIZE
        cmp     eax, 0FFF0h
        ja      .no
        clc
        jmp     .o
.no:    stc
.o:     pop     edx
        pop     ebx
        ret

;-----------------------------------------------------------------------------
; qic_init: a floppy tape drive on unit [qt_unit] of the controller at
; [qt_base]?  CF if not.
;-----------------------------------------------------------------------------
qic_init:
        call    qt_gf_init
        mov     dx, [qt_base]
        add     dx, QT_MSR
        in      al, dx
        cmp     al, 0FFh                ; no controller
        je      .none
        call    qt_hook
        call    qic_probe
        pushf
        call    qt_unhook
        mov     word [qt_old0e+2], 0
        popf
        ret
.none:  stc
        ret

qic_probe:
        call    qt_reset
        jc      .none
        call    qt_get_status
        jc      .none
        test    al, QS_ERROR | QS_NEWCART
        jz      .id
        call    qt_get_error            ; the reset at power on
        jc      .none
.id:    mov     al, QIC_VENDOR
        mov     cl, 16
        call    qt_report
        jc      .noven                  ; older drives do not know it
        mov     [qt_vendor], ax
.noven: call    qt_get_status           ; and clear what a refusal set
        test    al, QS_ERROR
        jz      .rom
        call    qt_get_error
.rom:   mov     al, QIC_ROM
        mov     cl, 8
        call    qt_report
        jc      .nocfg
        mov     [qt_rom], al
.nocfg: mov     al, QIC_CONFIG
        mov     cl, 8
        call    qt_report
        jc      .names
        mov     [qt_config], al
.names: mov     si, qt_vendors          ; the vendor's name
.vn:    lodsw
        cmp     ax, 0FFFFh
        je      .rev
        cmp     ax, [qt_vendor]
        je      .vf
        add     si, 8
        jmp     .vn
.vf:    push    cs
        pop     es
        mov     di, qt_inq_vendor
        mov     cx, 8
        rep     movsb
.rev:   mov     al, [qt_rom]            ; revision: the ROM version in hex
        mov     di, qt_inq_rev+2
        call    qt_hex8
        clc
        ret
.none:  stc
        ret

; AL as two hex digits at CS:DI
qt_hex8:
        push    ax
        shr     al, 4
        call    .dig
        pop     ax
        and     al, 0Fh
.dig:   add     al, '0'
        cmp     al, '9'
        jbe     .s
        add     al, 'A' - '9' - 1
.s:     mov     [di], al
        inc     di
        ret

qt_vendors      dw      0047h
                db      'COLORADO'
                dw      014Eh
                db      'CONNER  '
                dw      0005h
                db      'CONNER  '
                dw      0FFFFh
