;=============================================================================
; sony.inc - SCSI commands on a Sony CDU31A or CDU33A CD-ROM drive on its
; proprietary interface: Sony's card or the CD interface of Creative's
; early Sound Blaster Pro cards (/C:SONY)
;
; The protocol of Linux's cdu31a.c (Corey Minyard) for these drives:
;   base+0  read: status (bit 0 an attention, bit 1 a result, bit 2 data,
;           bit 7 busy); write: the command
;   base+1  read: result bytes (an attention's code first); write: the
;           parameters of the next command
;   base+2  read: data
;   base+3  read: FIFO status (bit 2 a result byte waits); write: control
;           (bit 0 an attention seen, bit 1 the result, bit 6 clear the
;           parameters, bit 7 reset)
; A command is its parameters and then its byte; the drive answers with
; two bytes, 20h and the error, or the count of the result bytes that
; follow, ten at most at a time.  READ (32h) gives its sectors as data
; first.  The drive tells, apart from commands, attentions: 80h a disc was
; loaded.  It is polled, without IRQ or DMA.
;
; The SCSI side is mke_exec (mke.inc) with the routines here for the
; drive.  The drive is offered on the first free SCSI ID as a CD-ROM drive
; with "SONY" and its product from its configuration.
;
; License: GNU General Public License version 2 only.
;=============================================================================

SY_ST_ATTN      equ     01h             ; status
SY_ST_RES       equ     02h
SY_ST_DATA      equ     04h
SY_ST_BUSY      equ     80h
SY_FI_RES       equ     04h             ; FIFO status: a result byte
SY_CT_ATTN      equ     01h             ; control
SY_CT_RES       equ     02h
SY_CT_PARAM     equ     40h
SY_CT_RESET     equ     80h

SY_CMD_CONFIG   equ     00h             ; vendor, product, revision, ...
SY_CMD_MECH     equ     03h             ; mechanical status
SY_CMD_SETPARAM equ     10h
SY_CMD_TOC      equ     20h             ; the TOC: 5 bytes an entry
SY_CMD_READTOC  equ     30h             ; spin up and read the TOC
SY_CMD_READ     equ     32h             ; MSF (BCD), count (3 bytes)

SY_MECH_DISC    equ     02h
SY_MECH_TOC     equ     10h
SY_ATT_LOADED   equ     80h

sy_chg          db      0               ; a disc was loaded
sy_par          times 6 db 0            ; parameters, a TOC entry
sy_lead         times 4 db 0            ; lead-out: ADR/control, MSF

;-----------------------------------------------------------------------------
; The drive on its ports
;-----------------------------------------------------------------------------

; -> AL = the status, AH = the FIFO status
sy_stat:
        push    dx
        mov     dx, [cd_base]
        add     dx, 3
        in      al, dx
        mov     ah, al
        sub     dx, 3
        in      al, dx
        pop     dx
        ret

; take the attentions the drive tells; a loaded disc sets sy_chg
sy_attn:
        push    ax
        push    cx
        push    dx
        mov     cx, 16
.l:     mov     dx, [cd_base]
        in      al, dx
        test    al, SY_ST_ATTN
        jz      .o
        add     dx, 3                   ; seen, then its code
        mov     al, SY_CT_ATTN
        out     dx, al
        sub     dx, 2
        in      al, dx
        cmp     al, SY_ATT_LOADED
        jne     .n
        mov     byte [sy_chg], 1
.n:     loop    .l
.o:     pop     dx
        pop     cx
        pop     ax
        ret

; wait, EBX ticks, until one of the bits AX (AL status, busy as not busy;
; AH FIFO status) is there.  -> AX = the status.  CF on timeout.
sy_wait:
        push    esi
        push    cx
        pushf
        sti
        push    ax
        call    get_ticks
        mov     esi, eax
        pop     cx
.l:     call    sy_attn
        call    sy_stat
        push    ax
        xor     al, SY_ST_BUSY
        and     ax, cx
        pop     ax
        jnz     .ok
        push    eax
        call    get_ticks
        sub     eax, esi
        cmp     eax, ebx
        pop     eax
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
sy_byte:
        push    ebx
        mov     ebx, CMD_TICKS
        mov     ax, SY_FI_RES << 8
        call    sy_wait
        jc      .o
        push    dx
        mov     dx, [cd_base]
        inc     dx
        in      al, dx
        pop     dx
        clc
.o:     pop     ebx
        ret

; CX result bytes away.  CF if they do not come.
sy_skip:
        jcxz    .o
.l:     call    sy_byte
        jc      .o
        loop    .l
.o:     ret

; the command AL with the CL parameters of sy_par, once the drive is no
; longer busy
sy_cmd: push    ebx
        push    cx
        push    dx
        push    si
        push    ax
        mov     ebx, CMD_TICKS
        mov     ax, SY_ST_BUSY
        call    sy_wait
        mov     dx, [cd_base]
        add     dx, 3
        mov     al, SY_CT_RES | SY_CT_PARAM
        out     dx, al
        sub     dx, 2
        mov     si, sy_par
        xor     ch, ch
        jcxz    .c
.p:     lodsb
        out     dx, al
        loop    .p
.c:     pop     ax
        dec     dx
        out     dx, al
        pop     si
        pop     dx
        pop     cx
        pop     ebx
        ret

; the first two bytes of the answer -> CX = the result bytes that follow.
; CF and EAX = the sense on an error (or none).
sy_begin:
        push    ebx
        mov     ebx, [tmo_ticks]        ; the drive may spin up first
        mov     ax, SY_ST_RES
        call    sy_wait
        jc      .lost
        call    sy_byte
        jc      .lost
        mov     ch, al
        call    sy_byte
        jc      .lost
        mov     cl, al
        mov     al, ch
        and     al, 0F0h
        cmp     al, 20h
        je      .err
        and     ch, 0Fh
        clc
        jmp     .o
.err:   mov     al, cl
        call    sy_sense
        stc
        jmp     .o
.lost:  mov     eax, SNS_ABORTED
        stc
.o:     pop     ebx
        ret

; the drive's error AL -> EAX = the sense
sy_sense:
        push    bx
        mov     bl, al
        mov     eax, SNS_NO_MEDIUM
        cmp     bl, 20h                 ; not loaded
        je      .o
        cmp     bl, 21h                 ; no disc
        je      .o
        mov     eax, SNS_BECOMING
        cmp     bl, 22h                 ; not spinning
        je      .o
        cmp     bl, 60h                 ; no TOC
        je      .o
        mov     eax, SNS_RANGE
        cmp     bl, 46h                 ; past the lead-out
        je      .o
        mov     eax, SNS_ABORTED
        cmp     bl, 11h                 ; command, parameter
        jbe     .o
        mov     eax, SNS_UNC
.o:     pop     bx
        ret

; the command AL with CL parameters; its result bytes, DX at most, into
; DI.  -> CX = their count.  CF and EAX = the sense if it fails.
sy_exec_cmd:
        push    dx
        push    di
        call    sy_cmd
        call    sy_begin
        jc      .o
        push    cx
.r:     jcxz    .d
        or      dx, dx
        jz      .s
        call    sy_byte
        jc      .lost
        mov     [di], al
        inc     di
        dec     dx
        dec     cx
        jmp     .r
.s:     call    sy_skip
        jc      .lost
.d:     pop     cx
        clc
.o:     pop     di
        pop     dx
        ret
.lost:  pop     cx
        mov     eax, SNS_ABORTED
        stc
        jmp     .o

; a disc in a drive that has its TOC; it spins up for it.  CF and EAX =
; the sense if not.
sy_ready:
        mov     al, SY_CMD_MECH
        xor     cl, cl
        mov     di, mk_res
        mov     dx, 3
        call    sy_exec_cmd
        jc      .o
        cmp     byte [sy_chg], 0
        je      .st
        mov     byte [sy_chg], 0
        mov     eax, SNS_CHANGED
        stc
        ret
.st:    test    byte [mk_res], SY_MECH_DISC
        jz      .none
        test    byte [mk_res], SY_MECH_TOC
        jnz     .o                      ; CF clear
        mov     al, SY_CMD_READTOC
        xor     cl, cl
        xor     dx, dx
        call    sy_exec_cmd
.o:     ret
.none:  mov     eax, SNS_NO_MEDIUM
        stc
        ret

; the capacity -> EAX = the first block after the disc.  CF and EAX = the
; sense if not.
sy_cap: call    sy_read_toc
        jc      .o
        mov     si, sy_lead+1
        call    mk_msf_lba
.o:     ret

; up to MK_BATCH sectors from mk_lba into x_buf.  CF and EAX = the sense
; if not.
sy_read:
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
        mov     [sy_par+5], al
        mov     word [sy_par+3], 0
        mov     eax, [mk_lba]
        mov     di, sy_par
        call    sy_lba_bcd
        mov     al, SY_CMD_READ
        mov     cl, 6
        call    sy_cmd
        mov     cx, [mk_cnt]
.sec:   mov     ebx, [tmo_ticks]
        mov     ax, SY_ST_DATA | SY_ST_RES
        call    sy_wait
        jc      .lost
        test    al, SY_ST_DATA
        jz      .end                    ; an answer first: the read failed
        mov     dx, [cd_base]
        add     dx, 2
        push    cx
        les     di, [x_buf]
        mov     cx, 2048
        cld
        rep     insb
        pop     cx
        add     word [x_buf+2], 2048 / 16
        add     dword [x_xfer], 2048
        inc     dword [mk_lba]
        dec     dword [mk_left]
        loop    .sec
.end:   mov     dx, cx                  ; the sectors missing
        call    sy_begin
        jc      .o
        call    sy_skip
        jc      .lost
        or      dx, dx                  ; an answer, but sectors missing
        jz      .o
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
sy_read_toc:
        mov     al, SY_CMD_TOC
        xor     cl, cl
        call    sy_cmd
        call    sy_begin
        jc      .o
        push    cs
        pop     es
        mov     di, mk_toc+4
        mov     bx, 0101h               ; first, last track
        mov     dword [sy_lead], 0
        mov     dl, [cdb_buf+6]         ; starting track
.rec:   cmp     cx, 5
        jb      .rest
        sub     cx, 5
        mov     si, sy_par
        mov     dh, 5
.b:     call    sy_byte
        jc      .lost
        mov     [si], al
        inc     si
        dec     dh
        jnz     .b
        mov     si, sy_par+2            ; its MSF, or a track in the first
        call    sy_bin3
        mov     al, [sy_par+1]          ; the point
        cmp     al, 0A0h
        je      .first
        cmp     al, 0A1h
        je      .last
        cmp     al, 0A2h
        je      .lead
        call    sy_bin
        cmp     al, dl
        jb      .rec
        cmp     di, mk_toc + 4 + 8 * MK_TRACKS
        jae     .rec
        mov     ah, al
        mov     al, [sy_par]
        call    sy_entry
        jmp     .rec
.first: mov     bl, [sy_par+2]
        jmp     .rec
.last:  mov     bh, [sy_par+2]
        jmp     .rec
.lead:  mov     eax, [sy_par]           ; ADR/control, point, MSF
        mov     [sy_lead], al
        shr     eax, 16
        mov     [sy_lead+1], ax
        mov     al, [sy_par+4]
        mov     [sy_lead+3], al
        jmp     .rec
.rest:  call    sy_skip
        jc      .lost
        cmp     dl, 0AAh
        je      .lo
        cmp     dl, bh
        ja      .bad
.lo:    mov     ax, [sy_lead+1]         ; the lead-out
        mov     [sy_par+2], ax
        mov     al, [sy_lead+3]
        mov     [sy_par+4], al
        mov     al, [sy_lead]
        mov     ah, 0AAh
        call    sy_entry
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
.lost:  mov     eax, SNS_ABORTED
        stc
        ret

; a descriptor at ES:DI, DI past it: AL = the drive's ADR/control (the
; nibbles the other way round), AH = the track, sy_par+2 its MSF (binary)
sy_entry:
        rol     al, 4
        mov     byte [di], 0
        mov     [di+1], al
        mov     [di+2], ah
        mov     byte [di+3], 0
        test    byte [cdb_buf+1], 02h   ; MSF
        jz      .lba
        mov     byte [di+4], 0
        mov     ax, [sy_par+2]
        mov     [di+5], ax
        mov     al, [sy_par+4]
        mov     [di+7], al
        jmp     .n
.lba:   mov     si, sy_par+2
        call    mk_msf_lba
        BSWAP_EAX
        mov     [di+4], eax
.n:     add     di, 8
        ret

; BCD AL -> binary AL
sy_bin: mov     ah, al
        shr     ah, 4
        and     al, 0Fh
        aad
        ret

; the three BCD bytes at SI to binary
sy_bin3:
        push    cx
        mov     cx, 3
.l:     mov     al, [si]
        call    sy_bin
        mov     [si], al
        inc     si
        loop    .l
        pop     cx
        ret

; the logical block EAX as MSF (BCD) to DS:DI
sy_lba_bcd:
        push    cx
        push    di
        call    mk_lba_msf
        mov     cx, 3
.l:     mov     al, [di]
        aam
        shl     ah, 4
        or      al, ah
        mov     [di], al
        inc     di
        loop    .l
        pop     di
        pop     cx
        ret

;-----------------------------------------------------------------------------
; sony_probe: is there a drive at cd_base?  Reset it as cdu31a.c does; it
; must then give its configuration, "SONY" and the product.  CF if not.
;-----------------------------------------------------------------------------
sony_probe:
        push    ebx
        push    cx
        push    dx
        push    si
        push    di
        mov     dx, [cd_base]
        in      al, dx
        cmp     al, 0FFh                ; nothing there
        je      .no
        add     dx, 3
        mov     al, SY_CT_RESET
        out     dx, al
        mov     ebx, 2                  ; let it come up
        call    fd_sleep
        call    sy_attn
        mov     al, SY_CMD_CONFIG
        xor     cl, cl
        mov     di, mk_toc              ; free yet
        mov     dx, 32
        call    sy_exec_cmd
        jc      .no
        cmp     cx, 32
        jb      .no
        mov     al, [mk_toc]            ; a vendor
        cmp     al, 'A'
        jb      .no
        cmp     al, 'Z'
        ja      .no
        push    cs
        pop     es
        mov     si, mk_toc              ; vendor, product
        mov     di, mk_inq+8
        mov     cx, 24
        rep     movsb
        mov     si, mk_toc+24           ; revision
        mov     cx, 4
.r:     lodsb
        cmp     al, ' '
        jae     .rc
        mov     al, ' '
.rc:    stosb
        loop    .r
        mov     word [sy_par], 0F00h    ; decode: 2048 byte sectors
        mov     al, SY_CMD_SETPARAM
        mov     cl, 2
        xor     dx, dx
        call    sy_exec_cmd
        mov     byte [sy_chg], 0        ; the drive's reset is no change
        clc
        jmp     .o
.no:    stc
.o:     pop     di
        pop     si
        pop     dx
        pop     cx
        pop     ebx
        ret
