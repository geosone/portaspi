;=============================================================================
; imm.inc - the Iomega IMM parallel port SCSI chip (ZIP Plus, ZIP 250)
;
; The sequences are those of Linux drivers/scsi/imm.c (Grant Guenther,
; David Campbell, Tim Waugh); each routine names its imm.c counterpart.
;
; The chip is addressed with IEEE 1284.3 command packets (CPP).  Bytes to
; the drive go in pairs, one on each edge of STROBE; bytes from it come in
; IEEE 1284 nibble or byte mode after a negotiation, or in EPP data cycles.
; The status register shows the SCSI bus as
;   88h  data out (the drive wants data)    98h  data in
;   A8h  command                            B8h  status / message in
; with REQ in bit 7 and BSY in bit 3.
;
; License: GNU General Public License version 2 only.
;=============================================================================

IMM_CPP_CONNECT     equ 0E0h            ; select device 0 in compatible mode
IMM_CPP_CONNECT_EPP equ 28h             ; select device 0 in EPP mode
IMM_CPP_DISCONNECT  equ 30h             ; deselect all devices

cpp_s1          db      0
cpp_s2          db      0
cpp_s3          db      0
imm_rd          db      0               ; the data phase is data in
imm_dp          db      0               ; there is a data phase

;-----------------------------------------------------------------------------
; imm_cpp(): command packet AL.  DX = base.
; -> AL = 1 connected, 0 disconnected, 0FFh no device
;-----------------------------------------------------------------------------
imm_cpp:
        push    bx
        mov     bl, al
        CTR     0Ch
        call    delay1
        mov     al, 0AAh
        out     dx, al
        call    delay1
        mov     al, 55h
        out     dx, al
        call    delay1
        xor     al, al
        out     dx, al
        call    delay1
        mov     al, 0FFh
        out     dx, al
        call    delay1
        inc     dx
        in      al, dx
        dec     dx
        and     al, 0B8h
        mov     [cpp_s1], al
        mov     al, 87h
        out     dx, al
        call    delay1
        inc     dx
        in      al, dx
        dec     dx
        and     al, 0B8h
        mov     [cpp_s2], al
        mov     al, 78h
        out     dx, al
        call    delay1
        inc     dx
        in      al, dx
        dec     dx
        and     al, 38h
        mov     [cpp_s3], al
        mov     al, bl                  ; the command, strobed
        out     dx, al
        call    delay1
        CTR     0Ch
        call    delay1
        CTR     0Dh
        call    delay1
        CTR     0Ch
        call    delay1
        mov     al, 0FFh
        out     dx, al
        call    delay1
        mov     al, 0FFh
        cmp     byte [cpp_s1], 0B8h
        jne     .out
        cmp     byte [cpp_s2], 18h
        jne     .out
        mov     al, 1
        cmp     byte [cpp_s3], 30h      ; connected
        je      .out
        dec     al
        cmp     byte [cpp_s3], 38h      ; disconnected
        je      .out
        mov     al, 0FFh
.out:   pop     bx
        ret

; imm_connect() -> AL as imm_cpp()
imm_connect:
        mov     al, IMM_CPP_CONNECT
        call    imm_cpp
        mov     al, IMM_CPP_DISCONNECT
        call    imm_cpp
        mov     al, IMM_CPP_CONNECT
        cmp     byte [lpt_mode], MODE_EPP
        jne     .n
        mov     al, IMM_CPP_CONNECT_EPP
.n:     jmp     imm_cpp

; imm_disconnect()
imm_disconnect:
        mov     al, IMM_CPP_DISCONNECT
        jmp     imm_cpp

; imm_reset_pulse(): SCSI bus reset
imm_reset_pulse:
        CTR     04h
        mov     al, 40h
        out     dx, al
        call    delay1
        CTR     0Ch
        CTR     0Dh
        push    cx
        mov     cx, 5                   ; 50 us
        call    delay15
        pop     cx
        CTR     0Ch
        CTR     04h
        ret

; imm_wait(): wait for REQ.  EBX = timeout in ticks.
; -> AL = status & B8h, CF on timeout
imm_wait:
        push    cx
        CTR     0Ch
        mov     ah, 80h
        mov     ch, 80h
        call    wait_bits
        pushf
        push    ax
        CTR     04h
        pop     ax
        popf
        pop     cx
        jc      .out
        and     al, 0B8h
.out:   ret

; imm_select(): select SCSI ID AL.  CF if no device answers.
imm_select:
        push    ebx
        push    cx
        mov     cl, al
        CTR     0Ch
        mov     ebx, SEL_TICKS
        mov     ah, 08h                 ; nothing holding the bus
        xor     ch, ch
        call    wait_bits
        jc      .out
        CTR     04h
        mov     al, 1
        shl     al, cl
        or      al, 80h                 ; host and target
        out     dx, al
        call    delay1
        CTR     0Ch                     ; SELECT IN off, then STROBE
        CTR     0Dh
        mov     ah, 08h                 ; the drive latches the bus
        mov     ch, 08h
        call    wait_bits
        pushf
        CTR     0Ch
        popf
.out:   pop     cx
        pop     ebx
        ret

; imm_negotiate(): IEEE 1284 negotiation into nibble or byte mode (not
; needed in EPP mode).  CF if the drive has no data.
imm_negotiate:
        cmp     byte [lpt_mode], MODE_EPP
        je      .ok
        CTR     04h
        call    delay1
        mov     al, 0                   ; nibble mode
        cmp     byte [lpt_mode], MODE_PS2
        jne     .m
        mov     al, 1                   ; byte mode
.m:     out     dx, al
        push    cx
        mov     cx, 8                   ; 100 us
        call    delay15
        pop     cx
        CTR     06h
        call    delay1
        inc     dx
        in      al, dx
        dec     dx
        push    ax
        call    delay1
        CTR     07h
        call    delay1
        CTR     06h
        pop     ax
        test    al, 20h
        jz      .no
.ok:    clc
        ret
.no:    stc
        ret

; the end of a transfer from the drive in nibble or byte mode (imm_engine()
; phases 7 and 8)
imm_reverse_end:
        cmp     byte [lpt_mode], MODE_EPP
        je      .out
        CTR     04h
        CTR     0Ch
        CTR     0Eh
        CTR     04h
.out:   ret

;-----------------------------------------------------------------------------
; Data bytes, CX of them at ES:DI (DI is advanced), DX = base.
; CF on an EPP timeout.
;-----------------------------------------------------------------------------
; imm_out() without its imm_wait: CX even
imm_put:
        cmp     byte [lpt_mode], MODE_EPP
        je      epp_put
        CTR     04h                     ; imm_byte_out()
        shr     cx, 1
.b:     mov     al, [es:di]
        out     dx, al
        CTR     05h
        mov     al, [es:di+1]
        out     dx, al
        CTR     00h
        add     di, 2
        loop    .b
        CTR     04h
        clc
        ret

; imm_in() without its imm_wait
imm_get:
        cmp     byte [lpt_mode], MODE_EPP
        je      epp_get
        CTR     04h
        cmp     byte [lpt_mode], MODE_PS2
        je      .ps2
.n:     CTR     06h                     ; imm_nibble_in()
        inc     dx
        in      al, dx
        dec     dx
        shr     al, 4
        mov     ah, al
        CTR     05h
        inc     dx
        in      al, dx
        dec     dx
        and     al, 0F0h
        or      al, ah
        stosb
        CTR     04h
        loop    .n
        jmp     .end
.ps2:   CTR     26h                     ; imm_byte_in()
        in      al, dx
        stosb
        CTR     25h
        loop    .ps2
.end:   CTR     0Ch
        clc
        ret

;-----------------------------------------------------------------------------
; imm_init(): find the chip on lpt_base and reset the SCSI bus behind it.
; CF if there is no IMM.
;-----------------------------------------------------------------------------
imm_init:
        mov     dx, [lpt_base]
        call    imm_connect
        cmp     al, 1
        jne     .no
        call    imm_reset_pulse
        mov     cx, 67                  ; 1 ms to settle
        call    delay15
        call    imm_disconnect
        call    delay15
        clc
        ret
.no:    call    imm_disconnect
        stc
        ret

; SC_RESET_DEV: reset the SCSI bus
imm_bus_reset:
        mov     dx, [lpt_base]
        call    imm_connect
        call    imm_reset_pulse
        mov     cx, 67
        call    delay15
        call    imm_disconnect
        call    delay15
        ret

;-----------------------------------------------------------------------------
; imm_exec: as ppa_exec, imm_engine() phases 1 to 8 in one go
;-----------------------------------------------------------------------------
imm_exec:
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
        call    set_bulk
        les     di, [x_buf]
        call    norm_buf

        mov     dx, [lpt_base]
        call    imm_connect             ; phase 1
        cmp     al, 1
        jne     .nosel_disc
        mov     al, [x_target]          ; phase 2
        call    imm_select
        jc      .nosel_disc

        ; phase 3: the command in byte pairs
        movzx   si, byte [cdb_len]
        inc     si
        and     si, 0FEh
        xor     bx, bx
.cmd:   push    bx
        mov     ebx, CMD_TICKS
        call    imm_wait
        pop     bx
        jc      .timeout
        and     al, 18h
        cmp     al, 08h                 ; BSY, from the host
        jne     .phase
        push    cs
        pop     es
        lea     di, [cdb_buf+bx]
        mov     cx, 2
        call    imm_put
        jc      .phase
        add     bx, 2
        cmp     bx, si
        jb      .cmd

        ; phase 5: data phase? reads need a negotiation first
        mov     ebx, [tmo_ticks]
        call    imm_wait
        jc      .timeout
        mov     ah, al
        and     ah, 10h
        setnz   byte [imm_rd]
        test    al, 20h
        setz    byte [imm_dp]
        cmp     byte [imm_dp], 0
        je      .status
        cmp     byte [imm_rd], 0
        je      .data
        call    imm_negotiate
        jc      .phase

        ; phase 6: data, until the drive goes to the status phase
.data:  mov     ebx, [tmo_ticks]
        call    imm_wait
        jc      .timeout
        cmp     al, 0B8h
        je      .data_end
        mov     ah, al
        and     ah, 88h
        cmp     ah, 88h
        jne     .phase
        and     al, 18h
        mov     bl, 1                   ; in: bytes
        mov     cx, 1
        cmp     byte [imm_rd], 0
        jne     .rd
        mov     bl, 2                   ; out: byte pairs
        mov     cx, 2
        cmp     al, 08h
        jne     .phase
        jmp     .go
.rd:    cmp     al, 18h
        jne     .phase
.go:    mov     ah, bl
        call    xfer_setup
        cmp     bl, 2
        je      .put
        call    imm_get
        jmp     .moved
.put:   call    imm_put
.moved: jc      .phase
        call    xfer_done
        jmp     .data
.data_end:
        cmp     byte [imm_rd], 0        ; phase 7
        je      .status
        call    imm_reverse_end

        ; phase 8: status byte, then the optional message byte
.status:
        mov     ebx, CMD_TICKS
        call    imm_wait
        jc      .timeout
        cmp     al, 0B8h
        jne     .phase
        call    imm_negotiate
        jc      .phase
        call    imm_wait                ; imm_in(): BSY and data in
        jc      .timeout
        and     al, 18h
        cmp     al, 18h
        jne     .phase
        push    cs
        pop     es
        mov     di, x_status
        mov     cx, 1
        call    imm_get
        jc      .phase
        mov     ebx, MSG_TICKS
        call    imm_wait
        jc      .done
        cmp     al, 0B8h
        jne     .done
        mov     di, x_msg
        mov     cx, 1
        call    imm_get
.done:  call    imm_reverse_end
        call    imm_disconnect
        mov     al, XR_OK
        jmp     .out
.timeout:
        call    imm_disconnect
        mov     al, XR_TIMEOUT
        jmp     .out
.phase: call    imm_disconnect
        mov     al, XR_HBA
        jmp     .out
.nosel_disc:
        call    imm_disconnect
        mov     al, XR_NOSEL
.out:   pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
