;=============================================================================
; ppa.inc - the Iomega PPA3 parallel port SCSI chip (ZIP 100 parallel)
;
; The sequences are those of Linux drivers/scsi/ppa.c (Grant Guenther,
; David Campbell, Tim Waugh), which in turn follows a trace of Iomega's DOS
; GUEST driver.  Each routine names its ppa.c counterpart.
;
; While the chip is connected and SELECT IN is set, the high nibble of the
; status register is the SCSI bus:
;   C0h  data out (the drive wants data)    D0h  data in
;   E0h  command                            F0h  status / message in
; Bit 6 drops while the drive is disconnected from the SCSI bus (seeking).
; The transfers are polled; no interrupts, no retries.
;
; License: GNU General Public License version 2 only.
;=============================================================================

ppa_wait:
        push    cx
        mov     ah, 0C0h
        mov     ch, 0C0h
        call    wait_bits
        pop     cx
        jc      .out
        and     al, 0F0h
.out:   ret

;-----------------------------------------------------------------------------
; Connection (DX = base)
;-----------------------------------------------------------------------------
; ppa_c_pulse()
c_pulse:
        out     dx, al
        add     dx, 2
        mov     al, 4
        out     dx, al
        mov     al, 6
        out     dx, al
        mov     al, 4
        out     dx, al
        mov     al, 0Ch
        out     dx, al
        sub     dx, 2
        ret

; ppa_d_pulse()
d_pulse:
        out     dx, al
        add     dx, 2
        mov     al, 0Ch
        out     dx, al
        mov     al, 0Eh
        out     dx, al
        mov     al, 0Ch
        out     dx, al
        mov     al, 4
        out     dx, al
        mov     al, 0Ch
        out     dx, al
        sub     dx, 2
        ret

; ppa_connect(): EPP mode connects with CFh, the others with 8Fh
ppa_connect:
        xor     al, al
        call    c_pulse
        mov     al, 3Ch
        call    c_pulse
        mov     al, 20h
        call    c_pulse
        mov     al, 8Fh
        cmp     byte [lpt_mode], MODE_EPP
        jne     .n
        mov     al, 0CFh
.n:     jmp     c_pulse

; ppa_disconnect()
ppa_disconnect:
        xor     al, al
        call    d_pulse
        mov     al, 3Ch
        call    d_pulse
        mov     al, 20h
        call    d_pulse
        mov     al, 0Fh
        jmp     d_pulse

; The chip echoes AUTOFD in status bit 3 once connected (ppa_engine phase
; 1, the check for an unplugged cable).  CF if it does not.
ppa_check:
        CTR     0Eh
        inc     dx
        in      al, dx
        dec     dx
        test    al, 08h
        jz      .no
        CTR     0Ch
        inc     dx
        in      al, dx
        dec     dx
        test    al, 08h
        jnz     .no
        clc
        ret
.no:    stc
        ret

; connect and check, for up to a second (ppa_engine phase 1).  CF if the
; chip does not answer.
ppa_connect_checked:
        push    esi
        push    eax
        call    get_ticks
        mov     esi, eax
.try:   call    ppa_connect
        call    ppa_check
        jnc     .ok
        call    ppa_disconnect
        call    get_ticks
        sub     eax, esi
        cmp     eax, 18
        jbe     .try
        pop     eax
        pop     esi
        stc
        ret
.ok:    pop     eax
        pop     esi
        clc
        ret

; ppa_reset_pulse(): SCSI bus reset
reset_pulse:
        mov     al, 40h
        out     dx, al
        CTR     08h
        mov     cx, 2                   ; 30 us
        call    delay15
        CTR     0Ch
        ret

; ppa_select(): select SCSI ID AL.  CF if no device answers.
ppa_select:
        push    ebx
        push    cx
        mov     cl, al
        mov     ebx, SEL_TICKS
        mov     ah, 40h                 ; the previous device off the bus
        xor     ch, ch
        call    wait_bits
        jc      .out
        mov     al, 1
        shl     al, cl
        out     dx, al
        CTR     0Eh
        CTR     0Ch
        mov     al, 80h                 ; this is NOT the initiator
        out     dx, al
        CTR     08h
        mov     ah, 40h
        mov     ch, 40h
        call    wait_bits
.out:   pop     cx
        pop     ebx
        ret

;-----------------------------------------------------------------------------
; Data bytes, CX of them at ES:DI (DI is advanced), DX = base.
; CF on an EPP timeout.
;-----------------------------------------------------------------------------
; ppa_out() without its ppa_wait
ppa_put:
        cmp     byte [lpt_mode], MODE_EPP
        je      .epp
.b:     mov     al, [es:di]             ; ppa_byte_out()
        inc     di
        out     dx, al
        add     dx, 2
        mov     al, 0Eh
        out     dx, al
        mov     al, 0Ch
        out     dx, al
        sub     dx, 2
        loop    .b
        clc
        ret
.epp:   jmp     epp_put

; ppa_in() without its ppa_wait
ppa_get:
        cmp     byte [lpt_mode], MODE_EPP
        je      .epp
        cmp     byte [lpt_mode], MODE_PS2
        je      .ps2
        add     dx, 2                   ; ppa_nibble_in()
.n:     mov     al, 4
        out     dx, al
        dec     dx
        in      al, dx
        and     al, 0F0h
        mov     ah, al
        inc     dx
        mov     al, 6
        out     dx, al
        dec     dx
        in      al, dx
        inc     dx
        shr     al, 4
        or      al, ah
        stosb
        loop    .n
        mov     al, 0Ch
        out     dx, al
        sub     dx, 2
        clc
        ret
.ps2:   add     dx, 2                   ; ppa_byte_in()
        mov     al, 25h
        out     dx, al
.p:     sub     dx, 2
        in      al, dx
        stosb
        add     dx, 2
        mov     al, 27h
        out     dx, al
        mov     al, 25h
        out     dx, al
        loop    .p
        mov     al, 4
        out     dx, al
        mov     al, 0Ch
        out     dx, al
        sub     dx, 2
        clc
        ret
.epp:   jmp     epp_get

; epp_reset(): clear the EPP timeout bit
;-----------------------------------------------------------------------------
; ppa_init(): find the chip on lpt_base and reset the SCSI bus behind it.
; CF if there is no chip.
;-----------------------------------------------------------------------------
ppa_init:
        mov     dx, [lpt_base]
        call    ppa_disconnect
        call    ppa_connect
        call    ppa_check
        pushf
        jc      .nores
        call    reset_pulse
.nores: mov     cx, 67                  ; 1 ms to settle
        call    delay15
        call    ppa_disconnect
        call    delay15
        popf
        ret

; SC_RESET_DEV: reset the SCSI bus (as ppa.c's bus reset)
ppa_bus_reset:
        mov     dx, [lpt_base]
        call    ppa_connect
        call    reset_pulse
        mov     cx, 67
        call    delay15
        call    ppa_disconnect
        call    delay15
        ret

;-----------------------------------------------------------------------------
; ppa_exec: run the CDB cdb_buf (cdb_len bytes) on SCSI ID x_target, data
; at x_buf (x_len bytes, direction x_dir).  ppa_engine() phases 1 to 6 in
; one go.
; -> AL = XR_*; with XR_OK the SCSI status in x_status, and x_xfer, x_over
;-----------------------------------------------------------------------------
ppa_exec:
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
        les     di, [x_buf]             ; normalize the buffer pointer
        call    norm_buf

        mov     dx, [lpt_base]
        call    ppa_connect_checked     ; phase 1
        jc      .nosel
        mov     al, [x_target]          ; phase 2
        call    ppa_select
        jc      .nosel_disc

        ; phase 3: the command bytes, each when the drive asks for it
        CTR     0Ch
        movzx   si, byte [cdb_len]
        xor     bx, bx
.cmd:   push    bx
        mov     ebx, CMD_TICKS
        call    ppa_wait
        pop     bx
        jc      .timeout
        and     al, 50h
        cmp     al, 40h                 ; not a phase from the host
        jne     .phase
        push    cs
        pop     es
        lea     di, [cdb_buf+bx]
        mov     cx, 1
        call    ppa_put
        jc      .phase
        inc     bx
        cmp     bx, si
        jb      .cmd

        ; phase 5: data, until the drive goes to the status phase
.data:  CTR     0Ch
        mov     ebx, [tmo_ticks]
        call    ppa_wait
        jc      .timeout
        cmp     al, 0F0h
        je      .status
        mov     ah, 2
        cmp     al, 0C0h
        je      .xfer
        mov     ah, 1
        cmp     al, 0D0h
        jne     .phase                  ; command phase again
.xfer:  mov     bl, ah
        mov     cx, 1
        call    xfer_setup
        cmp     bl, 2
        je      .put
        call    ppa_get
        jmp     .moved
.put:   call    ppa_put
.moved: jc      .phase
        call    xfer_done
        jmp     .data

        ; phase 6: status byte, then the optional message byte
.status:
        push    cs
        pop     es
        mov     di, x_status
        mov     cx, 1
        call    ppa_get
        jc      .phase
        mov     ebx, MSG_TICKS
        call    ppa_wait
        jc      .done
        cmp     al, 0F0h
        jne     .done
        mov     di, x_msg
        mov     cx, 1
        call    ppa_get
.done:  call    ppa_disconnect
        mov     al, XR_OK
        jmp     .out
.timeout:
        call    ppa_disconnect
        mov     al, XR_TIMEOUT
        jmp     .out
.phase: call    ppa_disconnect
        mov     al, XR_HBA
        jmp     .out
.nosel_disc:
        call    ppa_disconnect
.nosel: mov     al, XR_NOSEL
.out:   pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret

; x_buf = ES:DI with the offset below 16
