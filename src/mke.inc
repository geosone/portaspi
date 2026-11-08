;=============================================================================
; mke.inc - SCSI commands on a CD-ROM drive on the Panasonic
; (Matsushita-Kotobuki, MKE) interface: the CD interface of Creative's
; Sound Blaster Pro and 16 cards (/C:MKE) or Panasonic's own card (/C:PANA).
; The probe tells the drive's protocol: family 1 (CR-562, CR-563), family 0
; (CR-521), family 2 (Creative's CD-200, Funai's CD200F) or TEAC's CD-55A;
; each has a section below where it differs from family 1.
;
; The protocol of Linux's sbpcd.c (Eberhard Moenkeberg) for these drives:
;   base+0  write: command bytes, read: result bytes (and, on Creative's
;           interface, data once base+1 was given 1)
;   base+1  read: flags, active low: bit 2 a result byte waits, bit 1 data
;           waits; write (Creative): 1 data, 0 results through base+0
;   base+2  read: data (Panasonic's card); write: reset
;   base+3  write: the drive (0 here)
; Every command is seven bytes; the drive answers with its result bytes
; and a status byte (bit 4 an error, bit 6 a disc, bit 0 ready), READ
; with the sectors as data first.  After an error READ ERROR (82h) tells
; its code: 03h not ready, 11h the disc was changed.  The drive is polled,
; without IRQ or DMA.
;
; The drive is offered on the first free SCSI ID as a CD-ROM drive:
;   TEST UNIT READY                      the drive's status
;   INQUIRY                              the vendor and the drive's version
;   READ CAPACITY                        from the drive's capacity
;   READ (10)                            up to 16 sectors a command
;   READ TOC (format 0)                  every track, LBA or MSF
;   REZERO, SEEK, START STOP, PREVENT    nothing to do
;   ALLOW, RESERVE, RELEASE, MODE SELECT
;   REQUEST SENSE                        the sense of the last command
;
; License: GNU General Public License version 2 only.
;=============================================================================

MK_FL_NORES     equ     04h             ; no result byte waits
MK_FL_NODATA    equ     02h             ; no data wait

MK_CMD_STATUS   equ     05h
MK_CMD_READ     equ     10h             ; MSF, count
MK_CMD_ERROR    equ     82h             ; 8 bytes: the code in the third
MK_CMD_VERSION  equ     83h             ; 10 bytes: "CR-563" and firmware
MK_CMD_CAPACITY equ     85h             ; MSF of the end, block size
MK_CMD_INFO     equ     8Bh             ; type, first, last, lead-out MSF
MK_CMD_TOC      equ     8Ch             ; a track: 8 bytes

MK_ST_OK        equ     01h
MK_ST_ERROR     equ     10h
MK_ST_DISC      equ     40h

MK_ERR_READY    equ     03h
MK_ERR_CHANGE   equ     11h
MK_ERR_RESET    equ     12h             ; family 2: the drive was reset

MK_BATCH        equ     16              ; sectors a READ
MK_TRACKS       equ     99

mk_sb           db      0               ; Creative: data through base+0
mk_f0           db      0               ; a drive of family 0 (CR-521)
mk_f2           db      0               ; of family 2 (CD-200)
mk_stcmd        db      MK_CMD_STATUS   ; what differs in family 2
mk_stdisc       db      MK_ST_DISC
mk_stok         db      MK_ST_OK
mk_sterr        db      MK_ST_ERROR
mk_errcmd       db      MK_CMD_ERROR
mk_errlen       dw      8
mk_sns          dd      0               ; sense of the last command
mk_lba          dd      0
mk_left         dd      0
mk_cnt          dw      0
mk_cmd          times 10 db 0
mk_clen         dw      7               ; its length: 10 on a TEAC
mk_res          times 12 db 0
mk_inq          db      05h, 80h, 02h, 02h, 31, 0, 0, 0
                db      'MATSHITA'
                db      '                '      ; product
                db      '    '                  ; revision
mk_toc          times 4 + 8 * (MK_TRACKS + 1) db 0

; what differs on another interface (sony.inc): CF and EAX = the sense if
; it fails
cd_f_ready      dw      mk_ready        ; a disc, ready to read
cd_f_cap        dw      mk_cap          ; -> EAX = the first block after it
cd_f_read       dw      mk_read         ; sectors from mk_lba, mk_left
cd_f_toc        dw      mk_read_toc     ; READ TOC into mk_toc, DI its end

;-----------------------------------------------------------------------------
; mke_exec: the SCSI command cdb_buf with the request of dev_exec
;-----------------------------------------------------------------------------
mke_exec:
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
        mov     dword [mk_sns], 0       ; a new command clears the sense
        cmp     al, SCSI_INQUIRY
        je      .inquiry
        call    [cd_f_ready]            ; everything else asks the drive
        jc      .sense
        mov     al, [cdb_buf]
        cmp     al, 00h                 ; TEST UNIT READY
        je      .good
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
        je      .good
        inc     si
        jmp     .nop
.bad:   mov     eax, SNS_BAD_OPCODE
        jmp     .sense

.rsense:
        mov     eax, [mk_sns]
        mov     dword [mk_sns], 0
        mov     di, ata_reply
        call    ata_sense_data
        mov     cx, 18
        call    ata_give
        jmp     .good

.inquiry:
        test    byte [cdb_buf+1], 1     ; vital product data
        jnz     .badfield
        mov     si, mk_inq
        mov     cx, 36
        call    ata_give
        jmp     .good

.rcap:  call    [cd_f_cap]
        jc      .sense
        dec     eax                     ; the last block
        BSWAP_EAX
        mov     [ata_reply], eax
        mov     dword [ata_reply+4], 00080000h  ; 2048 byte blocks
        mov     si, ata_reply
        mov     cx, 8
        call    ata_give
        jmp     .good

.toc:   test    byte [cdb_buf+2], 0Fh   ; format 0 only
        jnz     .badfield
        test    byte [cdb_buf+9], 0C0h
        jnz     .badfield
        call    [cd_f_toc]
        jc      .sense
        mov     si, mk_toc
        mov     cx, di
        sub     cx, si
        call    ata_give
        jmp     .good

.read:  mov     eax, [cdb_buf+2]
        BSWAP_EAX
        movzx   ecx, word [cdb_buf+7]
        xchg    cl, ch
        mov     [mk_lba], eax
        mov     [mk_left], ecx
        mov     eax, [x_len]            ; what the buffer holds
        shr     eax, 11
        cmp     eax, ecx
        jae     .rd
        mov     [mk_left], eax
        mov     byte [x_over], 1
.rd:    cmp     dword [mk_left], 0
        je      .good
        call    [cd_f_read]
        jc      .sense
        jmp     .rd

.badfield:
        mov     eax, SNS_BAD_FIELD
.sense: mov     [mk_sns], eax
        shl     al, 4
        mov     [x_err], al
        mov     byte [x_status], STATUS_CHKCOND
.good:  mov     al, XR_OK
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret

;-----------------------------------------------------------------------------
; The drive on its ports
;-----------------------------------------------------------------------------

; the flags -> AL
mk_flags:
        push    dx
        mov     dx, [cd_base]
        inc     dx
        in      al, dx
        pop     dx
        ret

; wait, EBX ticks, until one of the flags AH drops.  -> AL = the flags.
; CF on timeout.
mk_wait:
        push    esi
        push    cx
        pushf
        sti
        push    ax
        call    get_ticks
        mov     esi, eax
        pop     ax
.l:     push    ax
        call    mk_flags
        pop     cx
        mov     cl, al
        and     al, ch
        cmp     al, ch
        mov     al, cl
        jne     .ok
        push    cx
        call    get_ticks
        pop     cx
        sub     eax, esi
        cmp     eax, ebx
        mov     ah, ch
        jbe     .l
        popf
        stc
        jmp     .o
.ok:    popf
        clc
.o:     pop     cx
        pop     esi
        ret

; -> AL = the next result byte.  CF if none comes.
mk_byte:
        push    ebx
        push    cx
        push    dx
        mov     ebx, CMD_TICKS
        mov     ah, MK_FL_NORES
        call    mk_wait
        jc      .o
        mov     dx, [cd_base]
        in      al, dx
        clc
.o:     pop     dx
        pop     cx
        pop     ebx
        ret

; results through base+0 (AL = 0) or, on Creative's interface, data (1)
mk_select:
        cmp     byte [mk_sb], 0
        je      .o
        push    dx
        mov     dx, [cd_base]
        inc     dx
        out     dx, al
        pop     dx
.o:     ret

; mk_cmd: opcode AL, no parameters
mk_cmdset:
        push    di
        push    cx
        push    es
        push    cs
        pop     es
        mov     di, mk_cmd
        stosb
        xor     al, al
        mov     cx, 9
        rep     stosb
        pop     es
        pop     cx
        pop     di
        ret

; away with what the drive still had to tell; results through base+0
mk_drain:
        push    cx
        push    dx
        xor     al, al
        call    mk_select
        mov     cx, 64
.l:     call    mk_flags
        test    al, MK_FL_NORES
        jnz     .o
        mov     dx, [cd_base]
        in      al, dx
        loop    .l
.o:     pop     dx
        pop     cx
        ret

; send the command mk_cmd, after what the drive still had to tell
mk_send:
        push    cx
        push    dx
        push    si
        call    mk_drain
        mov     dx, [cd_base]
        mov     si, mk_cmd
        mov     cx, [mk_clen]
.c:     lodsb
        out     dx, al
        loop    .c
        pop     si
        pop     dx
        pop     cx
        ret

; the command mk_cmd with CX result bytes into mk_res.  -> AL = the status.
; CF and EAX = the sense if it fails.
mk_exec_cmd:
        push    cx
        push    di
        call    mk_send
        mov     di, mk_res
        jcxz    .st
.r:     call    mk_byte
        jc      .lost
        mov     [di], al
        inc     di
        loop    .r
.st:    cmp     byte [mk_f0], 0
        jne     .f0
        call    mk_status_byte
.o:     pop     di
        pop     cx
        ret
.f0:    call    m0_check                ; family 0: no status byte
        jmp     .o
.lost:  mov     eax, SNS_ABORTED
        stc
        jmp     .o

; the status byte that ends a command -> AL.  CF and EAX = the sense if it
; tells an error (or does not come).
mk_status_byte:
        call    mk_byte
        jc      .lost
        test    al, [mk_sterr]
        jnz     mk_error
        clc
        ret
.lost:  mov     eax, SNS_ABORTED
        stc
        ret

; the drive tells an error: its code -> EAX = the sense, CF
mk_error:
        push    cx
        push    di
        mov     al, [mk_errcmd]
        call    mk_cmdset
        call    mk_send
        mov     di, mk_res
        mov     cx, [mk_errlen]
.r:     call    mk_byte
        jc      .lost
        mov     [di], al
        inc     di
        loop    .r
        call    mk_byte                 ; its status
        jc      .lost
        mov     eax, SNS_CHANGED
        cmp     byte [mk_res+2], MK_ERR_CHANGE
        je      .o
        cmp     byte [mk_res+2], MK_ERR_RESET
        je      .o
        mov     eax, SNS_NO_MEDIUM
        cmp     byte [mk_res+2], MK_ERR_READY
        je      .o
        mov     eax, SNS_UNC
        jmp     .o
.lost:  mov     eax, SNS_ABORTED
.o:     stc
        pop     di
        pop     cx
        ret

; a disc in a drive that is ready; it may spin up for the command
; timeout.  CF and EAX = the sense if not.
mk_ready:
        push    esi
        call    get_ticks
        mov     esi, eax
.st:    mov     al, [mk_stcmd]
        call    mk_cmdset
        xor     cx, cx
        call    mk_exec_cmd
        jc      .o
        test    al, [mk_stdisc]
        jz      .none
        test    al, [mk_stok]
        jnz     .o                      ; CF clear
        call    get_ticks
        sub     eax, esi
        cmp     eax, [tmo_ticks]
        jbe     .st
.none:  mov     eax, SNS_NO_MEDIUM
        stc
.o:     pop     esi
        ret

; the capacity -> EAX = the first block after the disc.  CF and EAX = the
; sense if not.
mk_cap: mov     al, MK_CMD_CAPACITY
        call    mk_cmdset
        mov     cx, 5
        call    mk_exec_cmd
        jc      .o
        mov     si, mk_res
        call    mk_msf_lba
.o:     ret

; up to MK_BATCH sectors from mk_lba into x_buf.  CF and EAX = the sense
; if not.
mk_read:
        push    ebx
        push    cx
        push    dx
        push    di
        push    es
        mov     eax, [mk_left]
        cmp     eax, MK_BATCH
        jbe     .n
        mov     eax, MK_BATCH
.n:     mov     [mk_cnt], ax
        mov     al, MK_CMD_READ
        cmp     byte [mk_f2], 0
        je      .op
        mov     al, M2_CMD_READ
.op:    call    mk_cmdset
        mov     eax, [mk_lba]
        mov     di, mk_cmd+1
        call    mk_lba_msf
        mov     al, [mk_cnt]
        cmp     byte [mk_f2], 0
        je      .c1
        mov     [mk_cmd+5], al          ; family 2: count (2 bytes), MSF
        mov     al, 2
.c1:    mov     [mk_cmd+6], al
        call    mk_send
        mov     cx, [mk_cnt]
.sec:   mov     ebx, [tmo_ticks]
        mov     ah, MK_FL_NORES | MK_FL_NODATA
        call    mk_wait
        jc      .lost
        test    al, MK_FL_NODATA
        jnz     .status                 ; a status first: the read failed
        mov     al, 1
        call    mk_select
        mov     dx, [cd_base]
        cmp     byte [mk_sb], 0
        jne     .port
        add     dx, 2
.port:  push    cx
        les     di, [x_buf]
        mov     cx, 2048
        cld
        rep     insb
        pop     cx
        xor     al, al
        call    mk_select
        add     word [x_buf+2], 2048 / 16
        add     dword [x_xfer], 2048
        inc     dword [mk_lba]
        dec     dword [mk_left]
        loop    .sec
.status:
        call    mk_status_byte
        jc      .o
        cmp     cx, 0                   ; a status, but sectors missing
        je      .o
        mov     eax, SNS_UNC
        stc
        jmp     .o
.lost:  mov     eax, SNS_ABORTED
        stc
.o:     pop     es
        pop     di
        pop     dx
        pop     cx
        pop     ebx
        ret

; READ TOC (format 0) of cdb_buf into mk_toc.  -> DI = its end.  CF and
; EAX = the sense if not.
mk_read_toc:
        cmp     byte [mk_f2], 0
        jne     .f2
        mov     al, MK_CMD_INFO
        call    mk_cmdset
        mov     cx, 6
        call    mk_exec_cmd
        jc      .o
        mov     bl, [mk_res+1]          ; first track
        mov     bh, [mk_res+2]          ; last track
        jmp     .tr
.f2:    mov     al, M2_CMD_INFO         ; family 2: the disc, 0ABh
        call    mk_cmdset
        mov     byte [mk_cmd+2], 0ABh
        mov     cx, 8
        call    mk_exec_cmd
        jc      .o
        mov     bl, [mk_res+3]
        mov     bh, [mk_res+4]
.tr:
        push    cs
        pop     es
        mov     di, mk_toc+4
        mov     al, [cdb_buf+6]         ; starting track
        cmp     al, 0AAh
        je      .lead
        ja      .bad
        cmp     al, bh
        ja      .bad
        or      al, al
        jnz     .from
        inc     al
.from:  cmp     al, bl
        jae     .t
        mov     al, bl
.t:     cmp     al, bh
        ja      .lead
        cmp     di, mk_toc + 4 + 8 * MK_TRACKS
        jae     .lead
        push    ax
        call    mk_toc_entry
        pop     ax
        jc      .o
        inc     al
        jmp     .t
.lead:  mov     al, 0AAh
        call    mk_toc_entry
        jc      .o
        mov     ax, di
        sub     ax, mk_toc+2            ; data length
        xchg    al, ah
        mov     [mk_toc], ax
        mov     [mk_toc+2], bl
        mov     [mk_toc+3], bh
        clc
.o:     ret
.bad:   mov     eax, SNS_BAD_FIELD
        stc
        ret

; the descriptor of track AL at ES:DI, DI past it.  CF and EAX = the sense
; if not.
mk_toc_entry:
        push    bx
        push    cx
        mov     bl, al
        cmp     byte [mk_f2], 0
        jne     .f2
        mov     al, MK_CMD_TOC
        call    mk_cmdset
        mov     [mk_cmd+2], bl
        cmp     byte [mk_f0], 0
        je      .x
        mov     byte [mk_cmd+1], 2      ; family 0: MSF, at byte 5
.x:     mov     cx, 8
        call    mk_exec_cmd
        jc      .o
        cmp     byte [mk_f0], 0
        je      .e
        mov     ax, [mk_res+5]
        mov     [mk_res+4], ax
        mov     al, [mk_res+7]
        mov     [mk_res+6], al
        jmp     .e
.f2:    mov     al, M2_CMD_INFO         ; family 2: ADR/control, track, MSF
        call    mk_cmdset
        mov     [mk_cmd+2], bl
        mov     cx, 5
        call    mk_exec_cmd
        jc      .o
        mov     al, [mk_res+4]
        mov     [mk_res+6], al
        mov     ax, [mk_res+2]
        mov     [mk_res+4], ax
        mov     al, [mk_res]
        mov     [mk_res+1], al
.e:
        mov     byte [di], 0
        mov     al, [mk_res+1]          ; ADR and control
        mov     [di+1], al
        mov     [di+2], bl
        mov     byte [di+3], 0
        test    byte [cdb_buf+1], 02h   ; MSF
        jz      .lba
        mov     byte [di+4], 0
        mov     ax, [mk_res+4]
        mov     [di+5], ax
        mov     al, [mk_res+6]
        mov     [di+7], al
        jmp     .n
.lba:   mov     si, mk_res+4
        call    mk_msf_lba
        BSWAP_EAX
        mov     [di+4], eax
.n:     add     di, 8
        clc
.o:     pop     cx
        pop     bx
        ret

; MSF (binary) at SI -> EAX = the logical block
mk_msf_lba:
        push    ebx
        movzx   ebx, byte [si]
        imul    ebx, ebx, 60
        movzx   eax, byte [si+1]
        add     ebx, eax
        imul    ebx, ebx, 75
        movzx   eax, byte [si+2]
        add     eax, ebx
        sub     eax, 150
        pop     ebx
        ret

; the logical block EAX as MSF (binary) to DS:DI
mk_lba_msf:
        push    ebx
        push    edx
        add     eax, 150
        xor     edx, edx
        mov     ebx, 75
        div     ebx
        mov     [di+2], dl              ; frame
        xor     edx, edx
        mov     ebx, 60
        div     ebx
        mov     [di+1], dl              ; second
        mov     [di], al                ; minute
        pop     edx
        pop     ebx
        ret

;-----------------------------------------------------------------------------
; Family 0 (CR-521): other opcodes, results without a status byte; the
; status is a command of its own, 81h, a single byte.  Its flags: bit 0
; high once a read is over.
;-----------------------------------------------------------------------------

M0_CMD_SPINUP   equ     05h             ; format, address
M0_CMD_READ     equ     02h             ; address, count (2 bytes), format
M0_CMD_STATUS   equ     81h
M0_CMD_CAPACITY equ     88h             ; blocks (3 bytes), block size
M0_FL_READING   equ     01h             ; low while a read is on

; the drive's status -> AL.  CF and EAX = SNS_ABORTED if none comes.
m0_status:
        push    dx
        call    mk_drain
        mov     dx, [cd_base]
        mov     al, M0_CMD_STATUS
        out     dx, al
        pop     dx
        call    mk_byte
        jnc     .o
        mov     eax, SNS_ABORTED
.o:     ret

; how the last command went.  CF and EAX = the sense if it failed: READ
; ERROR (82h), the code in the second of 6 bytes.
m0_check:
        call    m0_status
        jc      .o
        test    al, MK_ST_ERROR
        jz      .o                      ; CF clear
        push    cx
        push    di
        mov     al, MK_CMD_ERROR
        call    mk_cmdset
        call    mk_send
        mov     di, mk_res
        mov     cx, 6
.r:     call    mk_byte
        jc      .lost
        mov     [di], al
        inc     di
        loop    .r
        mov     al, [mk_res+1]
        mov     eax, SNS_CHANGED
        cmp     byte [mk_res+1], MK_ERR_CHANGE
        je      .e
        mov     eax, SNS_NO_MEDIUM
        cmp     byte [mk_res+1], MK_ERR_READY
        je      .e
        mov     eax, SNS_RANGE
        cmp     byte [mk_res+1], 06h    ; the address
        je      .e
        mov     eax, SNS_UNC
        jmp     .e
.lost:  mov     eax, SNS_ABORTED
.e:     pop     di
        pop     cx
        stc
.o:     ret

; a disc, ready: SPIN UP to block 0 tells a missing or changed disc
m0_ready:
        mov     al, M0_CMD_SPINUP
        call    mk_cmdset
        xor     cx, cx
        jmp     mk_exec_cmd

; the capacity -> EAX = the first block after the disc
m0_cap: mov     al, M0_CMD_CAPACITY
        call    mk_cmdset
        mov     cx, 5
        call    mk_exec_cmd
        jc      .o
        mov     eax, [mk_res-1]         ; blocks, big endian in 3 bytes
        BSWAP_EAX
        and     eax, 00FFFFFFh
.o:     ret

; up to MK_BATCH sectors from mk_lba into x_buf.  CF and EAX = the sense
; if not.
m0_read:
        push    ebx
        push    cx
        push    dx
        push    esi
        push    di
        push    es
        mov     eax, [mk_left]
        cmp     eax, MK_BATCH
        jbe     .n
        mov     eax, MK_BATCH
.n:     mov     [mk_cnt], ax
        mov     al, M0_CMD_READ
        call    mk_cmdset
        mov     eax, [mk_lba]           ; the block, 3 bytes
        mov     [mk_cmd+3], al
        mov     [mk_cmd+2], ah
        shr     eax, 16
        mov     [mk_cmd+1], al
        mov     al, [mk_cnt]
        mov     [mk_cmd+5], al
        call    mk_send
        mov     cx, [mk_cnt]
.sec:   call    get_ticks
        mov     esi, eax
.w:     call    mk_flags
        test    al, MK_FL_NODATA
        jz      .data
        test    al, M0_FL_READING
        jnz     .end                    ; over, a sector short: an error
        call    get_ticks
        sub     eax, esi
        cmp     eax, [tmo_ticks]
        jbe     .w
        mov     eax, SNS_ABORTED
        stc
        jmp     .o
.data:  mov     al, 1
        call    mk_select
        mov     dx, [cd_base]
        cmp     byte [mk_sb], 0
        jne     .port
        add     dx, 2
.port:  push    cx
        les     di, [x_buf]
        mov     cx, 2048
        cld
        rep     insb
        pop     cx
        xor     al, al
        call    mk_select
        add     word [x_buf+2], 2048 / 16
        add     dword [x_xfer], 2048
        inc     dword [mk_lba]
        dec     dword [mk_left]
        loop    .sec
.end:   push    cx
        call    m0_check
        pop     cx
        jc      .o
        jcxz    .o
        mov     eax, SNS_UNC
        stc
.o:     pop     es
        pop     di
        pop     esi
        pop     dx
        pop     cx
        pop     ebx
        ret

;-----------------------------------------------------------------------------
; Family 2 (Creative's CD-200, Funai's CD200F): family 1's packets and
; status byte, but other opcodes and status bits (bit 0 an error, bit 7 a
; disc, bit 1 readable); READ ERROR (03h) has 6 bytes.
;-----------------------------------------------------------------------------

M2_CMD_STATUS   equ     00h
M2_CMD_ERROR    equ     03h
M2_CMD_VERSION  equ     12h             ; 12 bytes: "CD200F " and firmware
M2_CMD_CAPACITY equ     25h             ; the last block, block size
M2_CMD_READ     equ     28h             ; MSF, count (2 bytes), 2
M2_CMD_INFO     equ     43h             ; 0ABh the disc, a track, 0AAh

; the capacity -> EAX = the first block after the disc
m2_cap: mov     al, M2_CMD_CAPACITY
        call    mk_cmdset
        mov     cx, 8
        call    mk_exec_cmd
        jc      .o
        mov     eax, [mk_res]
        BSWAP_EAX
        inc     eax
.o:     ret

; is it a drive of family 2?  Its version (after the reset) -> CF if not
m2_probe:
        mov     byte [mk_f2], 1         ; for mk_status_byte and mk_error
        mov     byte [mk_stcmd], M2_CMD_STATUS
        mov     byte [mk_stdisc], 80h
        mov     byte [mk_stok], 02h
        mov     byte [mk_sterr], 01h
        mov     byte [mk_errcmd], M2_CMD_ERROR
        mov     word [mk_errlen], 6
        mov     al, M2_CMD_VERSION
        call    mk_cmdset
        mov     cx, 12
        call    mk_exec_cmd
        jnc     .v
        cmp     eax, SNS_ABORTED        ; the reset it tells is an answer:
        je      .no                     ; read, it is gone; ask again
        mov     al, M2_CMD_VERSION
        call    mk_cmdset
        mov     cx, 12
        call    mk_exec_cmd
        jc      .no
.v:     cmp     word [mk_res], 'CD'
        jne     .no
        mov     word [cd_f_cap], m2_cap
        push    cs
        pop     es
        mov     si, m2_creative
        cmp     byte [mk_res+5], 'F'
        jne     .ven
        mov     si, m2_funai
.ven:   mov     di, mk_inq+8
        mov     cx, 8
        rep     movsb
        mov     si, mk_res              ; the product, up to a space
        mov     cx, 7
.p:     lodsb
        cmp     al, ' '
        jbe     .rev
        stosb
        loop    .p
.rev:   mov     si, mk_res+7
        mov     di, mk_inq+32
        mov     cx, 4
.r:     lodsb
        cmp     al, ' '
        jae     .rc
        mov     al, ' '
.rc:    stosb
        loop    .r
        clc
        ret
.no:    mov     byte [mk_f2], 0         ; family 1 again
        mov     byte [mk_stcmd], MK_CMD_STATUS
        mov     byte [mk_stdisc], MK_ST_DISC
        mov     byte [mk_stok], MK_ST_OK
        mov     byte [mk_sterr], MK_ST_ERROR
        mov     byte [mk_errcmd], MK_CMD_ERROR
        mov     word [mk_errlen], 8
        stc
        ret

m2_creative     db      'CREATIVE'
m2_funai        db      'FUNAI   '

;-----------------------------------------------------------------------------
; TEAC's CD-55A: ten-byte commands, nearly SCSI's.  The answer is a byte,
; 00h success or 02h an error, after the data (READ, READ TOC) or before
; the result bytes (the identification, the sense); the sense (03h) is
; SCSI's key, ASC and ASCQ in its bytes 2 to 4.
;-----------------------------------------------------------------------------

TC_CMD_READY    equ     00h
TC_CMD_SENSE    equ     03h
TC_CMD_IDENT    equ     12h             ; 00h, "CD-55A" and the firmware
TC_CMD_READ     equ     28h             ; READ (10)
TC_CMD_TOC      equ     43h             ; READ TOC

; the answer byte of a command.  CF and EAX = the sense if it is an error.
tc_answer:
        call    mk_byte
        jc      .lost
        or      al, al
        jz      .o                      ; CF clear
        cmp     al, 02h
        jne     .lost
        push    cx
        push    di
        mov     al, TC_CMD_SENSE
        call    mk_cmdset
        mov     byte [mk_cmd+4], 5
        call    mk_send
        mov     di, mk_res
        mov     cx, 5
.r:     call    mk_byte
        jc      .lost2
        mov     [di], al
        inc     di
        loop    .r
        mov     eax, [mk_res+1]         ; key, ASC, ASCQ
        shr     eax, 8
        pop     di
        pop     cx
        stc
        ret
.lost2: pop     di
        pop     cx
.lost:  mov     eax, SNS_ABORTED
        stc
.o:     ret

; a disc, ready
tc_ready:
        mov     al, TC_CMD_READY
        call    mk_cmdset
        call    mk_send
        jmp     tc_answer

; data, up to CX bytes to ES:DI, until the answer.  -> DI past them.  CF
; and EAX = the sense if it is an error.
tc_data:
        push    ebx
        push    dx
        mov     al, 1
        call    mk_select
        mov     dx, [cd_base]
        cmp     byte [mk_sb], 0
        jne     .l
        add     dx, 2
.l:     mov     ebx, [tmo_ticks]
        mov     ah, MK_FL_NORES | MK_FL_NODATA
        call    mk_wait
        jc      .lost
        test    al, MK_FL_NODATA
        jnz     .end
        in      al, dx
        jcxz    .l                      ; more than there is room for
        stosb
        dec     cx
        jmp     .l
.lost:  xor     al, al
        call    mk_select
        mov     eax, SNS_ABORTED
        stc
        jmp     .o
.end:   xor     al, al
        call    mk_select
        call    tc_answer
.o:     pop     dx
        pop     ebx
        ret

; READ TOC of cdb_buf, as it is, into mk_toc.  -> DI = its end.  CF and
; EAX = the sense if not.
tc_toc: push    cs
        pop     es
        mov     si, cdb_buf
        mov     di, mk_cmd
        mov     cx, 10
        rep     movsb
        mov     word [mk_cmd+7], (4 + 8 * (MK_TRACKS + 1)) >> 8 | ((4 + 8 * (MK_TRACKS + 1)) & 0FFh) << 8
        call    mk_send
        mov     di, mk_toc
        mov     cx, 4 + 8 * (MK_TRACKS + 1)
        jmp     tc_data

; the capacity -> EAX = the first block after the disc: the lead-out's
; descriptor
tc_cap: push    cs
        pop     es
        mov     al, TC_CMD_TOC
        call    mk_cmdset
        mov     byte [mk_cmd+6], 0AAh
        mov     byte [mk_cmd+8], 12
        call    mk_send
        mov     di, mk_toc
        mov     cx, 12
        call    tc_data
        jc      .o
        mov     eax, [mk_toc+8]
        BSWAP_EAX
.o:     ret

; up to MK_BATCH sectors from mk_lba into x_buf.  CF and EAX = the sense
; if not.
tc_read:
        push    ebx
        push    cx
        push    dx
        push    di
        push    es
        mov     eax, [mk_left]
        cmp     eax, MK_BATCH
        jbe     .n
        mov     eax, MK_BATCH
.n:     mov     [mk_cnt], ax
        mov     al, TC_CMD_READ
        call    mk_cmdset
        mov     eax, [mk_lba]
        BSWAP_EAX
        mov     [mk_cmd+2], eax
        mov     al, [mk_cnt]
        mov     [mk_cmd+8], al
        call    mk_send
        mov     cx, [mk_cnt]
.sec:   mov     ebx, [tmo_ticks]
        mov     ah, MK_FL_NORES | MK_FL_NODATA
        call    mk_wait
        jc      .lost
        test    al, MK_FL_NODATA
        jnz     .end                    ; the answer first: the read failed
        mov     al, 1
        call    mk_select
        mov     dx, [cd_base]
        cmp     byte [mk_sb], 0
        jne     .port
        add     dx, 2
.port:  push    cx
        les     di, [x_buf]
        mov     cx, 2048
        cld
        rep     insb
        pop     cx
        xor     al, al
        call    mk_select
        add     word [x_buf+2], 2048 / 16
        add     dword [x_xfer], 2048
        inc     dword [mk_lba]
        dec     dword [mk_left]
        loop    .sec
.end:   call    tc_answer
        jc      .o
        jcxz    .o
        mov     eax, SNS_UNC            ; done, but sectors missing
        stc
        jmp     .o
.lost:  mov     eax, SNS_ABORTED
        stc
.o:     pop     es
        pop     di
        pop     dx
        pop     cx
        pop     ebx
        ret

; is it a TEAC?  Its identification (after the reset) -> CF if not
tc_probe:
        mov     word [mk_clen], 10
        mov     al, TC_CMD_IDENT
        call    mk_cmdset
        mov     byte [mk_cmd+4], 12
        call    mk_send
        mov     di, mk_toc              ; free yet
        mov     cx, 12
.b:     call    mk_byte
        jc      .no
        mov     [di], al
        inc     di
        loop    .b
        cmp     byte [mk_toc], 0
        jne     .no
        cmp     word [mk_toc+1], 'CD'
        jne     .no
        mov     word [cd_f_ready], tc_ready
        mov     word [cd_f_cap], tc_cap
        mov     word [cd_f_read], tc_read
        mov     word [cd_f_toc], tc_toc
        push    cs
        pop     es
        mov     si, tc_teac
        mov     di, mk_inq+8
        mov     cx, 8
        rep     movsb
        mov     si, mk_toc+1            ; the product, 6 bytes
        mov     cx, 6
        rep     movsb
        mov     si, mk_toc+7            ; the firmware
        mov     di, mk_inq+32
        mov     cx, 4
        rep     movsb
        clc
        ret
.no:    mov     word [mk_clen], 7
        stc
        ret

tc_teac         db      'TEAC    '

;-----------------------------------------------------------------------------
; mke_probe: is there a drive of family 1 or 0 at cd_base?  Reset it as
; sbpcd.c does; it must then tell its version: "CR-56x" and the firmware,
; and its status (family 1), or "MATSHITA" and the firmware (family 0,
; the CR-521).  CF if not.
;-----------------------------------------------------------------------------
mke_probe:
        push    ebx
        push    cx
        push    dx
        push    si
        push    di
        mov     byte [mk_f0], 0
        call    mk_reset
        mov     al, MK_CMD_VERSION      ; the same in families 0 and 1
        call    mk_cmdset
        call    mk_send
        mov     di, mk_toc              ; free yet: what it tells
        xor     cx, cx
.b:     call    mk_byte
        jc      .got
        mov     [di], al
        inc     di
        inc     cx
        cmp     cx, 12
        jb      .b
.got:   push    cs
        pop     es
        mov     al, [mk_toc]            ; a name
        cmp     al, 'A'
        jb      .no
        cmp     al, 'Z'
        ja      .no
        cmp     cx, 12
        jb      .f1
        mov     si, mk_toc
        mov     di, m0_ven
        push    cx
        mov     cx, 8
        repe    cmpsb
        pop     cx
        jne     .f1
        mov     byte [mk_f0], 1         ; family 0
        mov     word [cd_f_ready], m0_ready
        mov     word [cd_f_cap], m0_cap
        mov     word [cd_f_read], m0_read
        mov     si, m0_prod
        mov     di, mk_inq+16
        mov     cx, 6
        rep     movsb
        mov     si, mk_toc+8
        jmp     .rev
.f1:    cmp     cx, 11                  ; family 1: ten bytes and the status
        jne     .no
        mov     si, mk_toc              ; the product, up to a space
        mov     di, mk_inq+16
        mov     cx, 6
.p:     lodsb
        cmp     al, ' '
        jbe     .r6
        stosb
        loop    .p
.r6:    mov     si, mk_toc+6            ; the firmware
.rev:   mov     di, mk_inq+32
        mov     cx, 4
.r:     lodsb
        cmp     al, ' '
        jae     .rc
        mov     al, ' '
.rc:    stosb
        loop    .r
        clc
        jmp     .o
.no:    call    mk_reset                ; family 2 then?
        call    m2_probe
        jnc     .o
        call    mk_reset                ; a TEAC?
        call    tc_probe
.o:     pop     di
        pop     si
        pop     dx
        pop     cx
        pop     ebx
        ret

; reset drive 0, as sbpcd.c does
mk_reset:
        push    ebx
        push    dx
        mov     dx, [cd_base]
        add     dx, 3                   ; drive 0
        xor     al, al
        out     dx, al
        dec     dx                      ; reset
        out     dx, al
        mov     ebx, 2                  ; let it come up
        call    fd_sleep
        pop     dx
        pop     ebx
        ret

m0_ven          db      'MATSHITA'
m0_prod         db      'CR-521'
