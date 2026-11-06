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

; The Iomega Ditto: a QIC-117 drive and a 765 behind a BackPack.  The
; 765 is at 40h-47h, register 00h bit 0 is its interrupt once register
; 0Fh has 10h, READ DATA puts the sectors into the 128 KB buffer from the
; address in 2Ch (three bytes, low first) for the length less one in 30h;
; the host reads it through A0h from the address in 28h.  The protocol of
; 86Box's lpt_ditto.c.
BP_REG_STAT     equ     00h
BP_STAT_INT     equ     01h
BP_REG_IRQCTL   equ     0Fh
BP_IRQ_SOFT     equ     10h
BP_REG_MEMADDR  equ     28h
BP_REG_DMAADDR  equ     2Ch
BP_REG_DMASIZE  equ     30h
BP_REG_MEMORY   equ     0A0h

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
QIC_SEL_RATE    equ     27              ; select rate, a parameter
QIC_TAPE_STAT   equ     33              ; the cartridge: format, length
QIC_FMT_MODE    equ     15              ; enter format mode
QIC_REF_BURST   equ     16              ; write the reference burst
QIC_PRIMARY     equ     30              ; enter primary mode
QIC_CALIBRATE   equ     36              ; calibrate tape length
QIC_FMT_SEGS    equ     37              ; report format segments

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
SNS_QT_WRITE    equ     03h | (0Ch << 8)        ; write error
SNS_QT_FULL     equ     0Dh | (00h << 8) | (02h << 16)  ; end of medium
SNS_QT_POS      equ     05h | (24h << 8)        ; not at a volume's start
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
qt_bp           db      0               ; 1: the 765 behind a BackPack (Ditto)
qt_cyl          db      0               ; the controller's cylinder
qt_vendor       dw      0
qt_rom          db      0
qt_config       db      0
qt_fdcver       db      0               ; VERSION: 90h an 82077 or later
qt_ccr          db      0               ; the data rate: 0 500 kbit/s, 3 1 Mbit/s
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
        cmp     al, 0Ah                 ; WRITE (6)
        je      .write
        cmp     al, 04h                 ; FORMAT MEDIUM
        je      .format
        cmp     al, 10h                 ; WRITE FILEMARKS
        je      .wfm
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
        call    qt_wclose               ; what was written ends here
        jc      .sense
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
        call    qt_wclose
        jc      .sense
        call    qt_read
        jc      .sense
        jmp     .good

.write: test    byte [cdb_buf+1], 1     ; fixed blocks: not offered
        jnz     .badfield
        cmp     byte [cdb_buf+2], 0     ; at most 65535 bytes
        jne     .badfield
        call    qt_load
        jc      .sense
        call    qt_write
        jc      .sense
        jmp     .good

.format:
        call    qt_format
        jc      .sense
        jmp     .good

.wfm:   call    qt_load
        jc      .sense
        mov     eax, [cdb_buf+1]        ; any filemarks: the volume ends
        and     eax, 0FFFFFF00h
        jz      .good                   ; none: nothing is held back
        call    qt_wclose
        jc      .sense
        jmp     .good

.space: call    qt_load
        jc      .sense
        call    qt_wclose
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

qt_nops         db      0Bh, 15h, 16h, 17h, 1Bh, 1Eh, 0FFh
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
        mov     byte [qt_wopen], 0      ; what was not written is lost
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
qt_first        dd      0               ; first data segment: the volume table
qt_last         dd      0               ; last data segment
qt_wopen        db      0               ; a volume is being written
qt_wseg         dd      0               ; the segment the buffer fills
qt_wfill        dw      0               ; with this many bytes
qt_wstart       dd      0               ; the volume's first segment
qt_wsize        dd      0               ; and its bytes so far
qt_nbad         dw      0
qt_badlist      times QT_MAXBAD * 8 db 0        ; segment, sector mask
qt_nvol         db      0
qt_vols         times QT_MAXVOL * 12 db 0       ; first, last segment, size
qt_vol          db      0               ; position: volume
qt_voff         dd      0               ; its byte
qt_cseg         dd      0               ; that byte's segment
qt_coff         dw      0               ; and offset in its data
qt_left         dd      0               ; bytes of a READ still to come

qt_buf          dw      0               ; the segment buffer, CS:qt_buf
qt_bufphys      dd      0               ; its physical address (1 KB aligned)
qt_bseg         dd      0FFFFFFFFh      ; the segment it holds
qt_bcap         dw      0               ; its data bytes

qt_running      db      0               ; the tape streams
qt_next         dd      0FFFFFFFFh      ; the segment it is about to pass
qt_track        db      0FFh            ; the head's track, 0FFh: unknown
qt_wait         dd      0               ; ticks for qt_ready_wait, 0: /T:
qt_target       dd      0
qt_ttrack       db      0
qt_tindex       dw      0
qt_rseg         dd      0
qt_tries        db      0
qt_rmask        dd      0
qt_rdone        dd      0               ; sectors in the buffer (or erased)
qt_wrmode       db      0               ; qt_read_seg writes the buffer
qt_verify       db      0               ; qt_read_seg checks: no ECC
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
        imul    bx, bx, 12
        push    eax
        mov     eax, [qt_vols+bx]
        mov     [qt_cseg], eax
        pop     eax
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
        imul    bx, bx, 12
        mov     eax, [qt_vols+bx+8]
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
        mov     eax, [qt_cseg]
        cmp     eax, [qt_bseg]
        je      .have
        call    qt_read_seg
        jc      .o
.have:  mov     ax, [qt_bcap]
        sub     ax, [qt_coff]
        ja      .take
        inc     dword [qt_cseg]         ; on to the next segment
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
        imul    ebx, ebx, 12
        mov     ebx, [qt_vols+ebx+4]    ; past its last segment
        inc     ebx
        mov     [qt_cseg], ebx
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

;-----------------------------------------------------------------------------
; Formatting
;
; FORMAT MEDIUM lays the cartridge out anew: the reference burst if it has
; none, then every track in turn wound to its start and formatted while it
; streams, a FORMAT of the 765 for each run of segments in a floppy
; cylinder (128 sectors, four segments), the IDs C/H/R/N from the buffer,
; ended by the DMA count as ftape does (the tape has no index).  The grid
; is the QIC-80 one, 600 segments a head and 4 a cylinder.  The tracks
; are those the drive takes a head seek to; the segments a track what it
; calibrates to (else what QIC-40/80 have for the length).  The format
; code follows from the size: 2 (a sector mask a segment) up to 6912
; segments of QIC-40/80, 4 (a list) up to 65535, else 6 and QIC-3010/3020
; always.  VERIFY reads every segment after: the sectors that do not read
; go into the bad sector map, a segment with more than three the whole of
; it.  Then the header, twice, and an empty volume table.
;-----------------------------------------------------------------------------

QT_FMT_TRACKS   db      72, 50, 40, 36, 28, 20, 0
qt_ftracks      db      0
qt_fspt         dw      0
qt_fstd         db      0
qt_fgap         db      0
qt_fseg         dd      0               ; the segment being formatted
qt_fend         dd      0               ; the end of its track
qt_fhdr         dd      0, 0, 0         ; header, its copy, volume table

qt_format:
        test    byte [qt_status], QS_WPROT
        jz      .rw
        mov     eax, SNS_WPROT
        stc
        ret
.rw:    mov     byte [qt_loaded], 0     ; the old layout is gone
        mov     byte [qt_wopen], 0
        mov     dword [qt_bseg], 0FFFFFFFFh
        mov     word [qt_nbad], 0
        mov     byte [qt_verify], 0
        call    qt_rewind
        jc      .o
        call    qt_pick_rate            ; the standard and the rate
        mov     al, QIC_TAPE_STAT
        mov     cl, 8
        call    qt_report
        jnc     .st
        call    qt_clear
        mov     al, 2                   ; QIC-80, if the drive cannot say
.st:    mov     [qt_fstd], al
        mov     al, QIC_FMT_MODE        ; the burst only in format mode
        call    qt_command
        jc      .hw
        call    qt_ready_wait
        jc      .o
        call    qt_get_status           ; the reference burst, if none
        test    al, QS_REFERENCED       ; (a head seek needs it)
        jnz     .refd
        mov     al, QIC_REF_BURST
        call    qt_command
        jc      .hw
        mov     dword [qt_wait], 2 * QT_WIND_TICKS
        call    qt_ready_wait
        mov     dword [qt_wait], 0
        jc      .o
.refd:  call    qt_ftracks_probe
        jc      .o
        call    qt_fspt_find
        jc      .o
        mov     al, [qt_fstd]           ; QIC-3010/3020: the format too
        and     al, 0Fh
        cmp     al, 3
        jb      .geo
        shl     al, 2
        inc     al
        test    byte [qt_fstd], 80h     ; wide
        jz      .sel
        add     al, 2
.sel:   push    ax
        mov     al, QIC_SEL_RATE
        call    qt_command
        pop     ax
        jc      .hw
        add     al, 2
        call    qt_command
        jc      .hw
        call    qt_ready_wait           ; not taken: the drive knows
        jnc     .geo
        call    qt_clear
.geo:   mov     ax, [qt_fspt]           ; the layout to read and write by
        mov     [qt_spt], ax
        mov     byte [qt_spc], 4
        mov     word [qt_sph], 600
        mov     byte [qt_fgap], 233
        test    byte [qt_fstd], 60h     ; flex lengths
        jz      .gap
        mov     byte [qt_fgap], 248
.gap:   xor     bl, bl                  ; track
.trk:   call    qt_ftrack
        jc      .fail
        inc     bl
        cmp     bl, [qt_ftracks]
        jb      .trk
        mov     al, QIC_PRIMARY
        call    qt_command
        jc      .hw
        call    qt_ready_wait
        jc      .o
        movzx   eax, byte [qt_ftracks]  ; the last segment
        movzx   ecx, word [qt_fspt]
        mul     ecx
        dec     eax
        mov     [qt_last], eax
        mov     al, 6                   ; the format code
        mov     cl, [qt_fstd]
        and     cl, 0Fh
        cmp     cl, 3
        jae     .code
        cmp     dword [qt_last], 65535
        jae     .code
        mov     al, 4
        cmp     dword [qt_last], 6912
        jae     .code
        mov     al, 2
.code:  mov     [qt_fmt], al
        test    byte [cdb_buf+1], 2     ; VERIFY
        jz      .hdr
        call    qt_fverify
        jc      .o
.hdr:   call    qt_fheader
        jc      .o
        call    qt_rewind
        jmp     .o
.fail:  call    qt_stop
        mov     al, QIC_PRIMARY
        call    qt_command
        mov     eax, SNS_QT_WRITE
        stc
        ret
.hw:    mov     eax, SNS_QT_HW
        stc
.o:     ret

; the error a refused command left, cleared
qt_clear:
        call    qt_get_status
        test    al, QS_ERROR
        jz      .o
        call    qt_get_error
.o:     ret

; the tracks: the highest the head seeks to, of those cartridges have
qt_ftracks_probe:
        mov     si, QT_FMT_TRACKS
.l:     mov     al, [si]
        or      al, al
        jz      .none
        mov     al, QIC_TRACK
        call    qt_command
        jc      .hw
        mov     al, [si]
        inc     al                      ; track n - 1, a parameter
        call    qt_command
        jc      .hw
        call    qt_ready_wait
        jnc     .found
        call    qt_clear
        inc     si
        jmp     .l
.found: mov     al, [si]
        mov     [qt_ftracks], al
        mov     byte [qt_track], 0FFh
        clc
        ret
.none:  mov     eax, SNS_QT_FORMAT
        stc
        ret
.hw:    mov     eax, SNS_QT_HW
        stc
        ret

; the segments a track: calibrated, or from the QIC-40/80 length
qt_fspt_find:
        mov     al, QIC_CALIBRATE
        call    qt_command
        jc      .table
        mov     dword [qt_wait], 2 * QT_WIND_TICKS
        call    qt_ready_wait
        mov     dword [qt_wait], 0
        jc      .table
        mov     al, QIC_FMT_SEGS
        mov     cl, 16
        call    qt_report
        jc      .table
        or      ax, ax
        jz      .table
        mov     [qt_fspt], ax
        clc
        ret
.table: call    qt_clear
        mov     al, [qt_fstd]           ; 205 or 307.5 ft
        mov     ah, al
        and     ah, 70h
        and     al, 0Fh
        mov     bx, 68
        cmp     al, 1
        je      .q40
        mov     bx, 100
        cmp     al, 2
        jne     .none
        cmp     ah, 10h
        je      .t
        cmp     ah, 20h
        jne     .none
        mov     bx, 150
        jmp     .t
.q40:   cmp     ah, 10h
        je      .t
        mov     bx, 102
        cmp     ah, 20h
        jne     .none
.t:     mov     [qt_fspt], bx
        clc
        ret
.none:  mov     eax, SNS_QT_FORMAT
        stc
        ret

; format track BL.  CF on failure.
qt_ftrack:
        push    bx
        mov     [qt_ttrack], bl
        mov     al, QIC_TRACK
        call    qt_command
        jc      .f
        mov     al, [qt_ttrack]
        add     al, 2
        call    qt_command
        jc      .f
        call    qt_ready_wait
        jc      .f
        mov     al, [qt_ttrack]
        mov     [qt_track], al
        mov     al, QIC_PHYS_REV        ; to where the track begins
        test    byte [qt_track], 1
        jz      .wind
        mov     al, QIC_PHYS_FWD
.wind:  call    qt_command
        jc      .f
        mov     dword [qt_wait], QT_WIND_TICKS
        call    qt_ready_wait
        mov     dword [qt_wait], 0
        jc      .f
        movzx   eax, byte [qt_ttrack]
        movzx   ecx, word [qt_fspt]
        mul     ecx
        mov     [qt_fseg], eax
        add     eax, ecx
        mov     [qt_fend], eax
        call    qt_forward
        jc      .f
.run:   mov     eax, [qt_fseg]          ; segments to the cylinder's end
        mov     ecx, eax
        and     ecx, 3
        neg     ecx
        add     ecx, 4
        mov     edx, [qt_fend]
        sub     edx, eax
        cmp     ecx, edx
        jbe     .n
        mov     ecx, edx
.n:     call    qt_fids                 ; CX segments
        jc      .f
        movzx   ecx, cx
        add     [qt_fseg], ecx
        mov     eax, [qt_fseg]
        cmp     eax, [qt_fend]
        jb      .run
        call    qt_stop                 ; it stops at the track's end
        clc
.f:     pop     bx
        ret

; format CX segments from [qt_fseg], streaming.  CF on failure.
qt_fids:
        push    cx
        push    di
        mov     di, [qt_buf]            ; the IDs
        mov     eax, [qt_fseg]
        push    cx
.seg:   push    eax
        xor     edx, edx
        movzx   ebx, word [qt_sph]
        div     ebx                     ; EAX = head, EDX = in it
        mov     bh, al
        mov     ax, dx
        div     byte [qt_spc]           ; AL = cylinder, AH = segment in it
        mov     bl, al
        mov     al, ah
        shl     al, 5
        inc     al                      ; the first sector
        mov     ah, 32
.id:    mov     [di], bl                ; C
        mov     [di+1], bh              ; H
        mov     [di+2], al              ; R
        mov     byte [di+3], 3          ; N
        add     di, 4
        inc     al
        dec     ah
        jnz     .id
        pop     eax
        inc     eax
        loop    .seg
        pop     cx
        mov     di, [qt_buf]
        shl     cx, 7                   ; 128 bytes a segment
        push    cx
        mov     byte [qt_wrmode], 1     ; the DMA reads memory
        call    qt_dma
        mov     byte [qt_wrmode], 0
        cmp     byte [qt_bp], 0
        je      .go
        call    qt_podput
.go:    call    qt_select
        call    qt_int_clear
        pop     cx
        mov     al, 4Dh                 ; FORMAT TRACK, MFM
        call    qt_out
        jc      .f
        mov     al, [qt_unit]
        call    qt_out
        jc      .f
        mov     al, 3                   ; 1024 bytes
        call    qt_out
        jc      .f
        mov     ax, cx
        shr     ax, 2                   ; the sectors
        call    qt_out
        jc      .f
        mov     al, [qt_fgap]
        call    qt_out
        jc      .f
        mov     al, 6Bh                 ; the filler
        call    qt_out
        jc      .f
        call    qt_result
        jmp     .o
.f:     stc
.o:     pop     di
        pop     cx
        ret

; VERIFY: every segment read, what does not read into the bad sector map
qt_fverify:
        mov     byte [qt_verify], 1
        xor     edx, edx
.l:     push    edx
        mov     eax, edx
        call    qt_read_seg
        pop     edx
        jc      .all
        movzx   cx, byte [qt_nera]      ; the erasures, sector numbers
        jcxz    .n
        xor     eax, eax                ; (no bad sectors before them yet)
        xor     si, si
.e:     movzx   ebx, byte [qt_era+si]
        bts     eax, ebx
        inc     si
        loop    .e
        call    qt_bad_add
        jmp     .n
.all:   mov     eax, 0FFFFFFFFh
        call    qt_bad_add
.n:     inc     edx
        cmp     edx, [qt_last]
        jbe     .l
        mov     byte [qt_verify], 0
        call    qt_stop
        clc
        ret

; the header into the first two good segments, the volume table, empty,
; into the next with room.  CF and EAX = the sense.
qt_fheader:
        xor     eax, eax
        xor     si, si
.find:  push    eax
        call    qt_badmask
        mov     ecx, eax
        pop     eax
        cmp     si, 8
        jae     .vt
        or      ecx, ecx                ; a header: no bad sector at all
        jnz     .next
        jmp     .take
.vt:    push    eax
        call    qt_capacity
        or      ax, ax
        pop     eax
        jz      .next
.take:  mov     [qt_fhdr+si], eax
        add     si, 4
        cmp     si, 12
        jae     .found
.next:  inc     eax
        cmp     eax, 16
        jb      .find
        mov     eax, SNS_QT_FORMAT
        stc
        ret
.found: push    cs                      ; the header
        pop     es
        mov     di, [qt_buf]
        mov     cx, 29 * 1024 / 2
        xor     ax, ax
        rep     stosw
        mov     si, [qt_buf]
        mov     dword [si], 0AA55AA55h
        mov     al, [qt_fmt]
        mov     [si+4], al
        mov     eax, [qt_fhdr]
        mov     [si+6], ax
        mov     eax, [qt_fhdr+4]
        mov     [si+8], ax
        mov     eax, [qt_fhdr+8]
        mov     [si+10], ax
        mov     eax, [qt_last]
        mov     [si+12], ax
        cmp     byte [qt_fmt], 6
        jne     .g
        mov     dword [si+6], 0
        mov     dword [si+10], 0
        mov     eax, [qt_fhdr]
        mov     [si+234], eax
        mov     eax, [qt_fhdr+4]
        mov     [si+238], eax
        mov     eax, [qt_fhdr+8]
        mov     [si+242], eax
        mov     eax, [qt_last]
        mov     [si+246], eax
.g:     mov     ax, [qt_fspt]
        mov     [si+24], ax
        mov     al, [qt_ftracks]
        mov     [si+26], al
        mov     eax, [qt_last]
        xor     edx, edx
        mov     ecx, 600
        div     ecx
        mov     [si+27], al             ; max floppy side
        mov     byte [si+28], 149       ; max floppy track
        mov     byte [si+29], 128       ; max floppy sector
        push    si
        lea     di, [si+30]
        mov     si, qt_flabel
        mov     cx, 44
        rep     movsb
        pop     si
        call    qt_stamp
        mov     [si+74], eax            ; when it was formatted
        call    qt_fbadmap
        mov     eax, [qt_fhdr]
        call    qt_fput
        jc      .o
        mov     eax, [qt_fhdr+4]
        call    qt_fput
        jc      .o
        push    cs                      ; the volume table: nothing in it
        pop     es
        mov     di, [qt_buf]
        mov     cx, 29 * 1024 / 2
        xor     ax, ax
        rep     stosw
        mov     eax, [qt_fhdr+8]
        call    qt_fput
.o:     ret

qt_flabel       db      'PORTASPI formatted                          '

; the bad sector map of the list into the header in the buffer
qt_fbadmap:
        mov     cx, [qt_nbad]
        or      cx, cx
        jnz     .go
        ret
.go:    xor     bx, bx
        mov     di, [qt_buf]
        add     di, 256                 ; formats 4 and 6: a list at 256
.e:     mov     edx, [qt_badlist+bx]
        mov     eax, [qt_badlist+bx+4]
        cmp     byte [qt_fmt], 2
        jne     .list
        push    di
        mov     di, dx                  ; format 2: a mask at 2048 + 4 seg
        shl     di, 2
        add     di, [qt_buf]
        mov     [di+2048], eax
        pop     di
        jmp     .n
.list:  cmp     eax, 0FFFFFFFFh
        jne     .secs
        mov     eax, edx                ; the whole segment
        shl     eax, 5
        inc     eax
        or      eax, 800000h
        mov     [di], eax
        add     di, 3
        jmp     .n
.secs:  push    ecx
        push    esi
        xor     esi, esi                ; the sector
.s:     bt      eax, esi
        jnc     .sn
        mov     ecx, edx
        shl     ecx, 5
        add     ecx, esi
        inc     ecx
        mov     [di], ecx
        add     di, 3
.sn:    inc     esi
        cmp     esi, 32
        jb      .s
        pop     esi
        pop     ecx
.n:     add     bx, 8
        dec     cx
        jnz     .e
        mov     byte [di], 0            ; the list's end
        mov     word [di+1], 0
        ret

; the buffer to segment EAX, its parity added.  CF and EAX = the sense.
qt_fput:
        push    eax
        call    qt_capacity
        mov     [qt_bcap], ax
        pop     eax
        mov     byte [qt_wrmode], 1
        call    qt_read_seg
        mov     byte [qt_wrmode], 0
        ret

;-----------------------------------------------------------------------------
; Writing
;
; A volume is written at the end of data, or at the start of a volume,
; which ends the cartridge there (as a tape written from the load point
; is new from there on).  WRITE (6) fills the segment buffer and writes
; each segment as it fills, with its parity; any filemark, REWIND, SPACE
; or READ end the volume: its last segment, and its entry in the volume
; table, the entries after it dropped.
;-----------------------------------------------------------------------------

; WRITE (6) of variable blocks.  CF and EAX = the sense.
qt_write:
        test    byte [qt_status], QS_WPROT
        jz      .rw
        mov     eax, SNS_WPROT
        stc
        ret
.rw:    movzx   ecx, word [cdb_buf+3]
        xchg    cl, ch
        cmp     ecx, [x_len]
        jbe     .len
        mov     ecx, [x_len]
        mov     byte [x_over], 1
.len:   or      ecx, ecx
        jz      .ok
        cmp     byte [qt_wopen], 0
        jne     .fill
        push    ecx
        call    qt_wbegin
        pop     ecx
        jnc     .fill
        cmp     eax, SNS_QT_FULL        ; no room for it at all
        je      .eom
        stc
        ret
.fill:  mov     eax, [qt_wseg]          ; the cartridge is full
        cmp     eax, [qt_last]
        jbe     .go
        jmp     .eom
.go:    push    ds
        lds     si, [x_buf]
.l:     mov     ax, [cs:qt_bcap]        ; room in the segment
        sub     ax, [cs:qt_wfill]
        movzx   eax, ax
        cmp     eax, ecx
        jbe     .c
        mov     eax, ecx
.c:     push    ecx
        mov     ecx, eax
        push    cs
        pop     es
        mov     di, [cs:qt_buf]
        add     di, [cs:qt_wfill]
        add     [cs:qt_wfill], ax
        add     [cs:qt_wsize], eax
        add     [cs:x_xfer], eax
        mov     edx, eax
        rep     movsb
        push    si                      ; DS:SI on: keep the offset small
        shr     si, 4
        mov     ax, ds
        add     ax, si
        mov     ds, ax
        pop     si
        and     si, 0Fh
        pop     ecx
        sub     ecx, edx
        mov     ax, [cs:qt_wfill]
        cmp     ax, [cs:qt_bcap]
        jb      .more
        push    ds
        push    cs
        pop     ds
        push    ecx
        call    qt_wflush               ; the segment is full
        pop     ecx
        pop     ds
        jc      .full
.more:  or      ecx, ecx
        jnz     .l
        pop     ds
.ok:    clc
.o:     ret
.full:  pop     ds                      ; what is left was not written
        cmp     eax, SNS_QT_FULL
        jne     .err
        or      ecx, ecx                ; it all went: the next one ends
        jz      .ok
.eom:   mov     eax, SNS_QT_FULL
        mov     [qt_sinfo], ecx         ; the residue: the rest of the block
        mov     byte [qt_svalid], 1
        mov     byte [qt_sflags], QT_FL_EOM
.err:   stc
        ret

; a volume at the position: where it starts, the volumes from here on
; dropped.  CF and EAX = the sense.
qt_wbegin:
        cmp     dword [qt_voff], 0      ; at a volume's start or the end
        je      .at
        mov     eax, SNS_QT_POS
        stc
        ret
.at:    mov     al, [qt_vol]
        cmp     al, [qt_nvol]
        jbe     .k
        mov     al, [qt_nvol]
.k:     cmp     al, QT_MAXVOL
        jb      .room
        mov     eax, SNS_QT_FULL
        stc
        ret
.room:  push    ax
        movzx   bx, al
        mov     eax, [qt_first]         ; after the volume table
        or      bx, bx
        jz      .s
        dec     bx
        imul    bx, bx, 12
        mov     eax, [qt_vols+bx+4]     ; or after the volume before
.s:     inc     eax
        mov     [qt_wseg], eax
        call    qt_wnext                ; a segment with room, or none
        pop     bx
        jc      .o
        mov     [qt_vol], bl
        mov     [qt_nvol], bl           ; the cartridge ends here
        mov     eax, [qt_wseg]
        mov     [qt_wstart], eax
        mov     dword [qt_wsize], 0
        mov     byte [qt_wopen], 1
.o:     ret

; the next segment for the buffer: [qt_wseg] on, one with room.  CF and
; EAX = the sense at the end of the cartridge.
qt_wnext:
        mov     dword [qt_bseg], 0FFFFFFFFh
        mov     word [qt_wfill], 0
.l:     mov     eax, [qt_wseg]
        cmp     eax, [qt_last]
        ja      .full
        call    qt_capacity
        mov     [qt_bcap], ax
        or      ax, ax
        jnz     .ok
        inc     dword [qt_wseg]
        jmp     .l
.ok:    clc
        ret
.full:  mov     eax, SNS_QT_FULL
        stc
        ret

; write the buffer to segment [qt_wseg] (filled up with zeros) and go on
; to the next.  CF and EAX = the sense.
qt_wflush:
        mov     di, [qt_buf]
        add     di, [qt_wfill]
        mov     cx, [qt_bcap]
        sub     cx, [qt_wfill]
        push    cs
        pop     es
        xor     al, al
        rep     stosb
        mov     eax, [qt_wseg]
        mov     byte [qt_wrmode], 1
        call    qt_read_seg
        mov     byte [qt_wrmode], 0
        jc      .o
        inc     dword [qt_wseg]
        call    qt_wnext
.o:     ret

; end the volume being written, if one is: its last segment and its entry
; in the volume table.  CF and EAX = the sense.
qt_wclose:
        cmp     byte [qt_wopen], 0
        jne     .open
        clc
        ret
.open:  mov     byte [qt_wopen], 0
        mov     ebx, [qt_wseg]          ; the last segment holding data
        cmp     word [qt_wfill], 0
        je      .last
        push    ebx
        call    qt_wflush
        pop     ebx
        jnc     .end
        cmp     eax, SNS_QT_FULL        ; written; no segment after it
        jne     .o
        jmp     .end
.last:  dec     ebx                     ; nothing in this one
        cmp     ebx, [qt_wstart]
        jae     .end
        mov     ebx, [qt_wstart]        ; an empty volume: one segment
.end:   movzx   si, byte [qt_vol]       ; the volume
        imul    si, si, 12
        mov     eax, [qt_wstart]
        mov     [qt_vols+si], eax
        mov     [qt_vols+si+4], ebx
        mov     eax, [qt_wsize]
        mov     [qt_vols+si+8], eax
        call    qt_vtbl_write
        jc      .o
        inc     byte [qt_nvol]
        inc     byte [qt_vol]           ; after the filemark: the end of data
        call    qt_vol_start
        clc
.o:     ret

; the entry of volume [qt_vol] (qt_vols) into the volume table, after the
; entries of the volumes before it; the rest of the table cleared.  CF
; and EAX = the sense.
qt_vtbl_write:
        mov     eax, [qt_first]
        call    qt_read_seg
        jc      .o
        mov     si, [qt_buf]            ; the [qt_vol]-th VTBL entry
        mov     cx, [qt_bcap]
        shr     cx, 7
        mov     dl, [qt_vol]
.e:     jcxz    .full
        cmp     dword [si], 'VTBL'
        jne     .other
        or      dl, dl
        jz      .here
        dec     dl
        jmp     .n
.other: cmp     dword [si], 'XTBL'      ; the others are kept
        je      .n
        cmp     dword [si], 'UTID'
        je      .n
        cmp     dword [si], 'EXVT'
        jne     .here                   ; the end of the table
.n:     add     si, 128
        dec     cx
        jmp     .e
.full:  mov     eax, SNS_QT_FULL
        stc
        ret
.here:  push    cs                      ; clear it and all after it
        pop     es
        mov     di, si
        push    cx
        shl     cx, 7
        xor     al, al
        rep     stosb
        pop     cx
        mov     dword [si], 'VTBL'
        movzx   bx, byte [qt_vol]
        imul    bx, bx, 12
        mov     eax, [qt_vols+bx]       ; first and last segment
        mov     edx, [qt_vols+bx+4]
        cmp     byte [qt_fmt], 6
        je      .big
        mov     [si+4], ax
        mov     [si+6], dx
        jmp     .desc
.big:   mov     [si+76], eax
        mov     [si+80], edx
        sub     edx, eax
        inc     edx
        mov     [si+4], edx             ; its segments
        mov     word [si+58], 113       ; QIC-113 rev. G
        mov     word [si+60], 7
.desc:  mov     eax, [qt_vols+bx+8]     ; the data's size, 8 bytes
        mov     [si+96], eax
        push    si
        lea     di, [si+8]              ; the description, blank padded
        mov     si, qt_vdesc
        mov     cx, 44
        rep     movsb
        pop     si
        mov     al, [qt_vol]            ; its number
        aam
        add     ax, '00'
        xchg    al, ah
        mov     [si+8+16], ax
        call    qt_stamp
        mov     [si+52], eax            ; the date
        mov     byte [si+56], 14h       ; not verified, segment spanning
        mov     byte [si+57], 1         ; cartridge 1 of 1
        mov     eax, [qt_first]
        mov     byte [qt_wrmode], 1
        call    qt_read_seg
        mov     byte [qt_wrmode], 0
.o:     ret

qt_vdesc        db      'PORTASPI volume 00                          '

; the time now, as the QIC-80 volume table has it -> EAX: the year from
; 1970 in the top 7 bits, the seconds of that year in the rest (months of
; 31 days)
qt_stamp:
        push    ebx
        push    ecx
        push    edx
        mov     ah, 04h                 ; the date, BCD
        int     1Ah
        jc      .zero
        push    dx
        mov     al, ch
        call    qt_bcd
        mov     bl, 100
        mul     bl
        push    ax
        mov     al, cl
        call    qt_bcd
        pop     bx
        xor     ah, ah
        add     ax, bx
        sub     ax, 1970
        jb      .zz
        movzx   ebx, ax
        shl     ebx, 25                 ; the year
        pop     dx
        mov     al, dh
        call    qt_bcd
        dec     al
        mov     cl, 31
        mul     cl                      ; (month - 1) * 31
        push    ax
        mov     al, dl
        call    qt_bcd
        dec     al
        xor     ah, ah
        pop     cx
        add     ax, cx                  ; + day - 1
        movzx   ecx, ax
        push    ecx
        mov     ah, 02h                 ; the time, BCD
        int     1Ah
        pop     eax
        jc      .z2
        push    dx
        push    cx
        imul    eax, eax, 24
        pop     cx
        push    cx
        push    eax
        mov     al, ch
        call    qt_bcd
        movzx   edx, al
        pop     eax
        add     eax, edx                ; hours
        imul    eax, eax, 60
        pop     cx
        push    eax
        mov     al, cl
        call    qt_bcd
        movzx   edx, al
        pop     eax
        add     eax, edx                ; minutes
        imul    eax, eax, 60
        pop     dx
        push    eax
        mov     al, dh
        call    qt_bcd
        movzx   edx, al
        pop     eax
        add     eax, edx                ; seconds
        or      eax, ebx
        jmp     .o
.zz:    pop     dx
.z2:
.zero:  xor     eax, eax
.o:     pop     edx
        pop     ecx
        pop     ebx
        ret

; AL (BCD) -> AL
qt_bcd: push    cx
        mov     cl, al
        shr     al, 4
        mov     ch, 10
        mul     ch
        and     cl, 0Fh
        add     al, cl
        pop     cx
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
        mov     byte [qt_track], 0FFh   ; drives differ in where the head
        mov     dword [qt_next], 0FFFFFFFFh  ; goes: seek it to the track again
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
        mov     dword [qt_next], 0FFFFFFFFh
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
        call    qt_pick_rate
        mov     word [qt_nbad], 0       ; the header has no bad sectors
        mov     word [qt_spt], 150      ; the QIC-80 grid until it is read
        mov     byte [qt_spc], 4
        mov     word [qt_sph], 600
        xor     eax, eax                ; the header: the first segment
.hs:    push    eax                     ; with the signature
        call    qt_read_seg
        jnc     .hsig
        cmp     byte [qt_ccr], 0        ; nothing read at 1 Mbit/s: try
        je      .hnext                  ; it at 500 kbit/s from the start
        xor     al, al
        call    qt_set_rate
        jnc     .h500
        mov     byte [qt_ccr], 0
        call    qt_clock
.h500:  pop     eax
        xor     eax, eax
        jmp     .hs
.hsig:
        mov     si, [qt_buf]
        cmp     dword [si], 0AA55AA55h
        je      .found
        cmp     dword [si], 82288228h   ; a Ditto 2GB's header
        jne     .hnext
        call    qt_d2g
        jmp     .found
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
        mov     eax, [qt_first]         ; the volume table
        call    qt_read_seg
        jc      .o
        call    qt_vtbl
        mov     byte [qt_loaded], 1
        mov     byte [qt_vol], 0
        call    qt_vol_start
.ok:    clc
.o:     ret

; the Ditto 2GB's header in the buffer: everything from offset 256 on is
; scrambled, byte i as ~b + (i mod 256) (as Linux ftape reads it back)
qt_d2g: push    bx
        push    cx
        push    si
        mov     si, [qt_buf]
        mov     cx, 256
.l:     cmp     cx, [qt_bcap]
        jae     .o
        mov     bx, si
        add     bx, cx
        mov     al, [bx]
        sub     al, cl
        not     al
        mov     [bx], al
        inc     cx
        jmp     .l
.o:     mov     dword [si], 0AA55AA55h
        pop     si
        pop     cx
        pop     bx
        ret

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
        movzx   eax, word [si+10]       ; first data segment
        cmp     byte [qt_fmt], 6
        jne     .f
        mov     eax, [si+242]
.f:     mov     [qt_first], eax
        movzx   eax, word [si+12]       ; last data segment
        cmp     byte [qt_fmt], 6
        jne     .l
        mov     eax, [si+246]
.l:     mov     [qt_last], eax
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
        xor     edx, edx                ; segment
.m:     mov     eax, [si]
        or      eax, eax
        jz      .mn
        call    qt_bad_add
.mn:    add     si, 4
        inc     edx
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
        xchg    eax, edx                ; EDX = segment
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

; segment EDX has the bad sectors EAX: into the list (sorted as the map)
qt_bad_add:
        push    bx
        mov     bx, [qt_nbad]
        or      bx, bx
        jz      .new
        shl     bx, 3
        cmp     [qt_badlist+bx-8], edx  ; the last entry's segment?
        jne     .new2
        or      [qt_badlist+bx-8+4], eax
        jmp     .o
.new:   xor     bx, bx
.new2:  cmp     word [qt_nbad], QT_MAXBAD
        jae     .o                      ; too many: read as good
        mov     [qt_badlist+bx], edx
        mov     [qt_badlist+bx+4], eax
        inc     word [qt_nbad]
.o:     pop     bx
        ret

; the bad sectors of segment EAX -> EAX
qt_badmask:
        push    bx
        push    cx
        mov     cx, [qt_nbad]
        mov     bx, qt_badlist
        jcxz    .none
.l:     cmp     [bx], eax
        je      .f
        add     bx, 8
        loop    .l
.none:  xor     eax, eax
        jmp     .o
.f:     mov     eax, [bx+4]
.o:     pop     cx
        pop     bx
        ret

; the data bytes of segment EAX -> AX (0 with three good sectors or
; fewer; the high half of EAX is lost)
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
        movzx   eax, word [si+4]        ; first and last segment
        movzx   ebx, word [si+6]
        cmp     byte [qt_fmt], 6
        jne     .se
        mov     eax, [si+76]
        mov     ebx, [si+80]
.se:    mov     [di], eax
        mov     [di+4], ebx
        mov     eax, [si+96]            ; the size of the data
        cmp     dword [si+100], 0
        je      .sz
        mov     eax, 0FFFFFFFFh
.sz:    or      eax, eax
        jnz     .put
        push    edx                     ; none: all its segments hold
        mov     edx, [di]
.sum:   cmp     edx, [di+4]
        ja      .sumd
        push    eax
        mov     eax, edx
        call    qt_capacity
        movzx   eax, ax
        pop     ebx
        add     eax, ebx
        inc     edx
        jmp     .sum
.sumd:  pop     edx
.put:   mov     [di+8], eax
        add     di, 12
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
        mov     [qt_rseg], eax
        call    qt_badmask
        mov     [qt_rmask], eax
        mov     eax, [qt_rseg]
        call    qt_capacity
        mov     [qt_bcap], ax
        or      ax, ax
        jnz     .read
        mov     eax, [qt_rseg]          ; no data in it: nothing to read
        cmp     [qt_next], eax
        jne     .done
        inc     dword [qt_next]
        jmp     .done
.read:  mov     byte [qt_tries], 3
        mov     dword [qt_rdone], 0     ; a new try goes on where one ended
        mov     byte [qt_nera], 0
        cmp     byte [qt_wrmode], 0
        je      .again
        call    qt_encode               ; writing: the parity first
.again: mov     eax, [qt_rseg]
        call    qt_locate
        jc      .o
        xor     bl, bl                  ; sector
.run:   cmp     bl, 32
        jae     .read_ok
        movzx   ecx, bl
        bt      [qt_rmask], ecx
        jc      .skip
        bt      [qt_rdone], ecx
        jnc     .start
.skip:  inc     bl
        jmp     .run
.start: call    qt_secpos               ; DI: where it goes
        xor     bh, bh                  ; a run of good sectors
.ext:   mov     cl, bl
        add     cl, bh
        cmp     cl, 32
        jae     .go
        movzx   ecx, cl
        bt      [qt_rmask], ecx
        jc      .go
        bt      [qt_rdone], ecx
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
        call    qt_mark
        add     bl, bh
        jmp     .run
.read_ok:
        cmp     byte [qt_wrmode], 0
        jne     .streams
        cmp     byte [qt_verify], 0
        jne     .streams
        call    qt_ecc                  ; a damaged sector is put right
        jc      .rderr
.streams:
        mov     eax, [qt_rseg]          ; the tape streams on
        inc     eax
        mov     [qt_next], eax
        xor     edx, edx
        movzx   ecx, word [qt_spt]
        div     ecx
        or      edx, edx                ; but stops at the end of a track
        jnz     .done
        mov     dword [qt_next], 0FFFFFFFFh
.done:  mov     eax, [qt_rseg]
        mov     [qt_bseg], eax
        clc
        jmp     .o
.err:   cmp     byte [qt_wrmode], 0
        jne     .err1
        cmp     al, 2                   ; a CRC error: the sectors before it
        jne     .err1                   ; are in, it is an erasure
        mov     bh, ah
        call    qt_mark
        add     bl, bh
        cmp     byte [qt_nera], 3
        jae     .rderr                  ; more than the parity can mend
        call    qt_secpos
        mov     ax, di
        sub     ax, [qt_buf]
        shr     ax, 10
        movzx   si, byte [qt_nera]
        mov     [qt_era+si], al
        inc     byte [qt_nera]
        mov     bh, 1
        call    qt_mark
        inc     bl
        jmp     .run                    ; on with the next, while it streams
.err1:  cmp     al, 1                   ; not found: find the segment again
        jne     .rderr
        call    qt_stop
        dec     byte [qt_tries]
        jnz     .again
.rderr: call    qt_stop
        mov     eax, SNS_QT_READ
        cmp     byte [qt_wrmode], 0
        je      .rf
        mov     eax, SNS_QT_WRITE
.rf:    stc
.o:     pop     di
        pop     si
        pop     edx
        pop     ecx
        pop     ebx
        ret

; sector BL of the segment being read -> DI: its place in the buffer,
; after the good sectors before it
qt_secpos:
        push    eax
        push    cx
        mov     eax, [qt_rmask]
        not     eax
        mov     cl, bl
        mov     di, [qt_buf]
.l:     or      cl, cl
        jz      .o
        shr     eax, 1
        jnc     .n
        add     di, 1024
.n:     dec     cl
        jmp     .l
.o:     pop     cx
        pop     eax
        ret

; sectors BL to BL + BH - 1 are done
qt_mark:
        push    ecx
        push    dx
        mov     dl, bl
        mov     dh, bh
.l:     or      dh, dh
        jz      .o
        movzx   ecx, dl
        bts     [qt_rdone], ecx
        inc     dl
        dec     dh
        jmp     .l
.o:     pop     dx
        pop     ecx
        ret

%include "qicecc.inc"

; get the tape streaming just before segment EAX.  CF and EAX = the sense.
qt_locate:
        push    ebx
        push    ecx
        push    edx
        push    si
        mov     [qt_target], eax
        cmp     byte [qt_running], 0
        je      .stopped
        cmp     eax, [qt_next]          ; it is coming
        je      .ok
        call    qt_stop
        jc      .o
.stopped:
        mov     eax, [qt_target]
        xor     edx, edx
        movzx   ecx, word [qt_spt]
        div     ecx                     ; EAX = track, EDX = segment in it
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
        mov     ebx, [qt_target]
        dec     ebx                     ; the segment before the target
        cmp     eax, ebx
        je      .ok
        ja      .back
        mov     edx, ebx                ; still before it
        sub     edx, eax
        cmp     edx, 4
        ja      .fwd
        dec     si
        jnz     .id
.fwd:   call    qt_stop                 ; skip on to it
        jc      .o
        call    qt_skipmax
        dec     dx
        jz      .retry
        mov     al, QIC_SKIP_FWD
        call    qt_skip
        jc      .o
        jmp     .retry
.back:  call    qt_stop                 ; past it: back
        jc      .o
        mov     edx, eax
        sub     edx, ebx
        inc     edx
        call    qt_skipmax
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
.ok:    mov     eax, [qt_target]
        mov     [qt_next], eax
        clc
.o:     pop     si
        pop     edx
        pop     ecx
        pop     ebx
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
        mov     dword [qt_next], 0FFFFFFFFh
        clc
        ret
.hw:    mov     eax, SNS_QT_HW
        stc
        ret

; EDX segments to skip, at most what three nibbles say (the head is on
; the right track: a READ ID that far away is wrong, the next one less so)
qt_skipmax:
        cmp     edx, 0FFFh
        jbe     .o
        mov     edx, 0FFFh
.o:     ret

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

; READ ID -> EAX = the segment under the head (qt_idsec its sector).  CF
; if there is none.
qt_readid:
        push    ebx
        push    ecx
        push    edx
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
        movzx   ebx, al
        movzx   ax, byte [qt_res+3]     ; C
        mul     byte [qt_spc]
        movzx   eax, ax
        add     ebx, eax
        movzx   eax, byte [qt_res+4]    ; H
        movzx   ecx, word [qt_sph]
        mul     ecx
        add     eax, ebx
        clc
.o:     pop     edx
        pop     ecx
        pop     ebx
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
        push    ecx
        push    edx
        movzx   cx, bh
        shl     cx, 10
        call    qt_dma
        call    qt_podfill
        mov     eax, [qt_rseg]
        xor     edx, edx
        movzx   ecx, word [qt_sph]
        div     ecx                     ; EAX = head, EDX = segment in it
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
        cmp     byte [qt_wrmode], 0
        je      .cmd
        mov     al, 45h                 ; WRITE DATA, MFM
.cmd:   call    qt_out
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
        jc      .rerr
        call    qt_podcopy
        clc
        jmp     .o
.rerr:  cmp     al, 2                   ; a data error: in sector R
        jne     .fs
        mov     ah, [qt_res+5]
        sub     ah, dl                  ; AH = the sectors before it
        cmp     ah, bh
        jb      .part
        xor     ah, ah                  ; R outside the run: none of it
.part:  push    bx
        mov     bh, ah
        call    qt_podcopy
        pop     bx
        mov     al, 2
.fs:    stc
        jmp     .o
.f:     mov     al, 3
.o:     pop     edx
        pop     ecx
        pop     bx
        ret

; the Ditto, writing: BH sectors from CS:DI into its buffer, at the same
; offset; each byte clocked in as bk_sput does (not register 04h bit 7's
; burst, in which a change of the data lines also takes a byte)
qt_podfill:
        cmp     byte [qt_wrmode], 0
        je      .o
        cmp     byte [qt_bp], 0
        je      .o
        pushad
        push    es
        movzx   cx, bh
        shl     cx, 10
        call    qt_podput
        pop     es
        popad
.o:     ret

; CX bytes from CS:DI into the Ditto's buffer, at the same offset
qt_podput:
        pushad
        push    es
        mov     dx, [lpt_base]
        mov     ax, di
        sub     ax, [qt_buf]
        movzx   eax, ax
        push    cx
        mov     bl, BP_REG_MEMADDR
        call    fd_set3
        pop     cx
        push    cs
        pop     es
        mov     al, BP_REG_MEMORY
        call    bk_sput
        pop     es
        popad
        ret

; the Ditto: BH sectors from its buffer to CS:DI, at the same offset
qt_podcopy:
        cmp     byte [qt_wrmode], 0
        jne     .o
        cmp     byte [qt_bp], 0
        je      .o
        or      bh, bh
        jz      .o
        pushad
        push    es
        movzx   cx, bh
        shl     cx, 10
        mov     dx, [lpt_base]
        mov     ax, di
        sub     ax, [qt_buf]
        movzx   eax, ax
        push    bx
        mov     bl, BP_REG_MEMADDR
        call    fd_set3
        pop     bx
        push    cs
        pop     es
        mov     al, BP_REG_MEMORY
        call    bk_sget
        pop     es
        popad
.o:     ret

; DMA channel 2, controller to memory: CX bytes to CS:DI
qt_dma:
        push    eax
        cmp     byte [qt_bp], 0
        je      .isa
        push    bx                      ; the Ditto: into its buffer, at the
        push    dx                      ; offset the data has in ours
        mov     dx, [lpt_base]
        mov     ax, di
        sub     ax, [qt_buf]
        movzx   eax, ax
        mov     bl, BP_REG_DMAADDR
        call    fd_set3
        movzx   eax, cx
        dec     eax
        mov     bl, BP_REG_DMASIZE
        call    fd_set3
        pop     dx
        pop     bx
        pop     eax
        ret
.isa:   mov     ax, di
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
        cmp     byte [qt_wrmode], 0
        je      .mode
        mov     al, 4Ah                 ; or read it
.mode:  out     0Bh, al
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
        cmp     byte [qt_bp], 0
        jne     .dor
        push    ds
        push    word 40h
        pop     ds
        mov     byte [40h], 0           ; the motor time-out
        and     byte [3Fh], 0F0h        ; the motors that run
        pop     ds
.dor:   mov     al, [qt_unit]
        or      al, QT_DOR_RUN
        mov     ah, QT_DOR
        call    qt_wr
        pop     ax
        ret

; the controller's register AH (0-7): on the ISA bus at [qt_base], or the
; Ditto's at 40h-47h of its BackPack (connected).  qt_rd -> AL, qt_wr AL.
qt_rd:  push    dx
        cmp     byte [qt_bp], 0
        jne     .bp
        movzx   dx, ah
        add     dx, [qt_base]
        in      al, dx
        pop     dx
        ret
.bp:    push    bx
        mov     bh, ah
        add     ah, 40h
        mov     dx, [lpt_base]
        call    bpck_rd
        mov     ah, bh
        pop     bx
        pop     dx
        ret

qt_wr:  push    dx
        push    ax
        cmp     byte [qt_bp], 0
        jne     .bp
        movzx   dx, ah
        add     dx, [qt_base]
        out     dx, al
        jmp     .o
.bp:    add     ah, 40h
        mov     dx, [lpt_base]
        call    bpck_wr
.o:     pop     ax
        pop     dx
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
.l:     mov     ah, QT_MSR
        call    qt_rd
        and     al, 0C0h
        cmp     al, 80h                 ; RQM, to the controller
        je      .w
        call    get_ticks
        sub     eax, esi
        cmp     eax, CMD_TICKS
        jbe     .l
        stc
        jmp     .o
.w:     mov     ah, QT_MSR + 1          ; the data register
        mov     al, bl
        call    qt_wr
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
.l:     mov     ah, QT_MSR
        call    qt_rd
        and     al, 0C0h
        cmp     al, 0C0h                ; RQM, from the controller
        je      .r
        call    get_ticks
        sub     eax, esi
        cmp     eax, CMD_TICKS
        jbe     .l
        stc
        jmp     .o
.r:     mov     ah, QT_MSR + 1
        call    qt_rd
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
        cmp     byte [qt_bp], 0
        je      .isa
        push    ax                      ; the Ditto: connect, and let
        push    dx                      ; register 00h show its interrupt
        mov     dx, [lpt_base]
        call    bpck_connect
        mov     ax, (BP_REG_IRQCTL << 8) | BP_IRQ_SOFT
        call    bpck_wr
        mov     word [qt_old0e+2], 1
        pop     dx
        pop     ax
        ret
.isa:   push    ax
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
        cmp     byte [qt_bp], 0
        je      .isa
        push    dx
        mov     dx, [lpt_base]
        call    bpck_disconnect
        pop     dx
        ret
.isa:   push    ax
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
.l:     cmp     byte [qt_bp], 0
        je      .isa
        push    dx                      ; the Ditto: register 00h bit 0
        mov     dx, [lpt_base]
        mov     ah, BP_REG_STAT
        call    bpck_rd
        pop     dx
        test    al, BP_STAT_INT
        jnz     .ok
        jmp     .t
.isa:   cmp     byte [qt_irq], 0
        jne     .ok
.t:     call    get_ticks
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
        mov     al, [qt_unit]
        mov     ah, QT_DOR
        call    qt_wr                   ; reset
        mov     cx, 2
        call    delay15
        mov     al, [qt_unit]
        or      al, QT_DOR_RUN
        mov     ah, QT_DOR
        call    qt_wr
        mov     ebx, QT_INT_TICKS
        call    qt_wait_int
        jc      .o
        mov     cx, 4                   ; one for each drive
.si:    call    qt_sense_int
        jc      .o
        loop    .si
        call    qt_clock                ; the rate the drive runs at
        jc      .o
        mov     byte [qt_cyl], 0
        cmp     byte [qt_bp], 0
        jne     .ok
        push    ds                      ; the BIOS must recalibrate its drives
        push    word 40h
        pop     ds
        and     byte [3Eh], 70h
        pop     ds
.ok:    clc
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
        cmp     byte [qt_bp], 0
        jne     .ctl
        mov     dx, [qt_base]
        add     dx, QT_MSR
        in      al, dx
        cmp     al, 0FFh                ; no controller
        je      .none
.ctl:   call    qt_hook
        call    qic_probe
        pushf
        call    qt_unhook
        mov     word [qt_old0e+2], 0
        popf
        ret
.none:  stc
        ret

;-----------------------------------------------------------------------------
; The data rate
;
; A cartridge is read at a rate its standard has (QIC-80 and QIC-3010 at
; 500 kbit/s or 1 Mbit/s, QIC-3020 at 1 or 2 Mbit/s), that the drive takes
; (SELECT RATE, then its configuration says it) and that the controller
; has: 1 Mbit/s needs an 82077 or later (VERSION 90h).  2 Mbit/s (an
; 82078-1 and its drive specification table) is not done.
;-----------------------------------------------------------------------------

; the controller at rate qt_ccr: CCR and SPECIFY (2 ms steps at that
; clock, unload, head load, DMA).  CF on timeout.
qt_clock:
        mov     al, [qt_ccr]
        mov     ah, QT_CCR
        call    qt_wr
        mov     al, 03h                 ; SPECIFY
        call    qt_out
        jc      .o
        mov     al, 0E1h                ; 500 kbit/s: step rate 14
        cmp     byte [qt_ccr], 3
        jne     .s
        mov     al, 0C1h                ; 1 Mbit/s: 12
.s:     call    qt_out
        jc      .o
        mov     al, 02h
        call    qt_out
.o:     ret

; drive and controller to rate AL (a CCR code, 0 or 3).  CF if the drive
; does not take it; the controller then stays as it was.
qt_set_rate:
        push    bx
        push    cx
        mov     bh, al
        mov     bl, 2                   ; QIC-117: 2 is 500 kbit/s,
        cmp     al, 3
        jne     .c
        mov     bl, 3                   ; 3 is 1 Mbit/s
.c:     mov     al, QIC_SEL_RATE
        call    qt_command
        jc      .f
        mov     al, bl
        add     al, 2                   ; a parameter
        call    qt_command
        jc      .f
        call    qt_ready_wait           ; an error: refused
        jc      .f
        mov     al, QIC_CONFIG          ; and the drive says it runs so
        mov     cl, 8
        call    qt_report
        jc      .f
        mov     [qt_config], al
        shr     al, 3
        and     al, 3
        cmp     al, bl
        jne     .f
        mov     [qt_ccr], bh
        call    qt_clock
        jmp     .o
.f:     stc
.o:     pop     cx
        pop     bx
        ret

; the fastest rate cartridge, drive and controller have in common
qt_pick_rate:
        push    cx
        mov     al, QIC_TAPE_STAT       ; the standard; QIC-80 if the drive
        mov     cl, 8                   ; cannot say
        call    qt_report
        jnc     .std
        call    qt_get_status           ; clear the refusal
        test    al, QS_ERROR
        jz      .q80
        call    qt_get_error
.q80:   mov     al, 2
.std:   and     al, 0Fh
        cmp     al, 1                   ; QIC-40: 500 kbit/s at most
        je      .slow
        cmp     byte [qt_fdcver], 90h
        jne     .slow
        mov     al, 3
        call    qt_set_rate
        jnc     .o
.slow:  xor     al, al
        call    qt_set_rate
        jnc     .o
        mov     byte [qt_ccr], 0        ; a drive without SELECT RATE
        call    qt_clock
.o:     pop     cx
        ret

; qt_ditto_probe: is the pod (connected) a Ditto: a 765 at 40h whose
; interrupt shows in register 00h?  -> qt_bp = 1.  CF if not.
qt_ditto_probe:
        push    ax
        push    dx
        mov     byte [qt_bp], 1
        mov     byte [qt_unit], 0
        mov     dx, [lpt_base]
        mov     ax, (BP_REG_IRQCTL << 8) | BP_IRQ_SOFT
        call    bpck_wr
        call    qt_reset
        jnc     .o
        mov     byte [qt_bp], 0
.o:     pop     dx
        pop     ax
        ret

qic_probe:
        mov     byte [qt_ccr], 0
        call    qt_reset
        jc      .none
        mov     al, 10h                 ; VERSION: 80h (invalid) on a 765
        call    qt_out
        jc      .none
        call    qt_in
        jc      .none
        mov     [qt_fdcver], al
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
                dw      8881h           ; Iomega: the Ditto's report
                db      'IOMEGA  '
                dw      8883h
                db      'IOMEGA  '
                dw      8885h
                db      'IOMEGA  '
                dw      0FFFFh
