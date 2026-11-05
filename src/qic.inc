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
;   REWIND                               seek load point
;   MODE SENSE (6)                       write protection, variable blocks
;   READ BLOCK LIMITS                    1 to 65535 bytes
;   REQUEST SENSE                        the sense of the last command
; A new cartridge is reported once as UNIT ATTENTION.
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
SNS_BECOMING    equ     02h | (04h << 8) | (01h << 16)

qt_base         dw      3F0h
qt_unit         db      0               ; drive select line
qt_id           db      0FFh            ; SCSI ID, 0FFh: no tape drive
qt_cyl          db      0               ; the controller's cylinder
qt_vendor       dw      0
qt_rom          db      0
qt_config       db      0
qt_sns          dd      0               ; sense of the last command
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
        cmp     al, 1Ah                 ; MODE SENSE (6)
        je      .msense
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
        mov     al, QIC_LOAD_POINT
        call    qt_command
        jc      .hw
        call    qt_ready_wait
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
        mov     eax, SNS_CHANGED
        stc
        ret
.again: call    qt_get_status
        jc      .hw
.st:    test    al, QS_CART
        jz      .none
        test    al, QS_READY
        jz      .busy
        clc
        ret
.none:  mov     eax, SNS_NO_MEDIUM
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
        push    esi
        call    get_ticks
        mov     esi, eax
.l:     call    qt_get_status
        jc      .hw
        test    al, QS_READY
        jnz     .rdy
        call    get_ticks
        sub     eax, esi
        cmp     eax, [tmo_ticks]
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

; the drive's select line, without a motor
qt_select:
        push    ax
        push    dx
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
qt_st3: mov     al, 04h
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
; qic_init: a floppy tape drive on unit [qt_unit] of the controller at
; [qt_base]?  CF if not.
;-----------------------------------------------------------------------------
qic_init:
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
