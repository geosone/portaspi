;=============================================================================
; PORTASPI.SYS - ASPI manager for parallel port devices
;
; ASPI manager (SCSIMGR$) for SCSI devices behind a parallel port chip.
; This version knows Iomega's chips: the PPA3 of the ZIP 100 parallel port
; drive (ppa.inc) and the IMM of the ZIP Plus and ZIP 250 (imm.inc); and
; the Shuttle EPAT bridge to an ATAPI drive, as in the parallel port
; SuperDisk LS-120 (epat.inc), and the Micro Solutions BackPack bridge, as
; in the BackPack CD-ROM (bpck.inc); ATAPI on both in atapi.inc.  It works alone or together with other ASPI
; managers: it chains to a manager loaded before it, and managers loaded
; after it can chain to it.
;
; The ASPI layer is taken from EUSBASPI.SYS; its author licenses it for
; PORTASPI under the GPL version 2 as well.  The PPA protocol follows
; Linux drivers/scsi/ppa.c and imm.c (Grant Guenther, David Campbell, Tim
; Waugh).
;
; Build:  nasm -f bin -Isrc/ -o PORTASPI.SYS src/portaspi.asm
;
; CONFIG.SYS:
;   DEVICE=PORTASPI.SYS [/P:addr] [/M:NIBBLE|PS2|EPP] [/T:s] [/ID:name] [/Q] [/V]
;
;   /P:addr  parallel port base address in hex (default: the LPT ports in
;            the BIOS data area, the first one with a drive)
;   /M:mode  port mode: NIBBLE (any port), PS2 (bidirectional), EPP
;            (default: PS2 where the port is bidirectional, else NIBBLE)
;   /T:s     command timeout in seconds (default 15)
;   /ID:name ASPI manager id (default PORTASPI)
;   /Q       quiet, /V verbose
;
; Copyright 2026 Mario Fetka.
; License: GNU General Public License version 2 only.
;=============================================================================

        cpu     386
        bits    16
        org     0

%include "aspi.inc"

%define VERSION_STR '0.5'

STACK_SIZE      equ     0800h           ; BIOS IRQ handlers run on it too
NTARGET         equ     7               ; SCSI IDs 0..6, the adapter is 7
HA_TARGET_ID    equ     7

; result codes of ppa_exec
XR_OK           equ     0               ; status phase reached, x_status
XR_TIMEOUT      equ     2
XR_HBA          equ     3               ; protocol / phase error
XR_NOSEL        equ     4               ; no device at that SCSI ID

; port modes
MODE_NIBBLE     equ     0
MODE_PS2        equ     1
MODE_EPP        equ     2

; DOS request header
RQ_CMD          equ     02h
RQ_STATUS       equ     03h
RQ_XFER         equ     0Eh
RQ_COUNT        equ     12h
RQ_END          equ     0Eh             ; init: end of resident part
RQ_CMDLINE      equ     12h             ; init: command line

SCSI_REQUEST_SENSE equ  03h
SCSI_INQUIRY    equ     12h

;=============================================================================
; Resident part
;=============================================================================

hdr_next        dd      -1
hdr_attr        dw      0C000h          ; character device, IOCTL supported
hdr_strat       dw      strategy
hdr_intr        dw      interrupt
hdr_name        db      'SCSIMGR$'

req_ptr         dd      0

;-----------------------------------------------------------------------------
; DOS device driver entry points
;-----------------------------------------------------------------------------
strategy:
        mov     [cs:req_ptr], bx
        mov     [cs:req_ptr+2], es
        retf

interrupt:
        pushf
        push    ax
        push    bx
        push    di
        push    ds
        push    es
        lds     bx, [cs:req_ptr]
        mov     al, [bx+RQ_CMD]
        or      al, al
        jnz     .not_init
        call    word [cs:init_vec]
        jmp     .exit
.not_init:
        cmp     al, 3                   ; IOCTL input: return entry point
        je      .ioctl_in
        cmp     al, 0Dh                 ; open
        je      .done
        cmp     al, 0Eh                 ; close
        je      .done
        mov     word [bx+RQ_STATUS], 8103h ; unknown command
        jmp     .exit
.ioctl_in:
        les     di, [bx+RQ_XFER]
        mov     word [es:di], aspi_entry
        mov     [es:di+2], cs
        mov     word [bx+RQ_COUNT], 4
.done:
        mov     word [bx+RQ_STATUS], 0100h
.exit:
        pop     es
        pop     ds
        pop     di
        pop     bx
        pop     ax
        popf
        retf

init_vec        dw      init

;-----------------------------------------------------------------------------
; Resident data
;-----------------------------------------------------------------------------
mgr_id          db      'PORTASPI        '      ; 16 bytes, /ID: overrides
ha_ident        db      'PPA 0378 nibble '      ; 16 bytes: chip, port, mode
ha_unique       dw      0000h           ; buffer alignment mask (none)
                db      0               ; reserved
                db      NTARGET + 1     ; max targets
                dd      10000h          ; max transfer length
                times 8 db 0

tmo_ticks       dd      15 * 182 / 10   ; command timeout in BIOS ticks

; devices by SCSI ID: peripheral device type, 0FFh if there is none
dev_type        times NTARGET db 0FFh

; previously loaded ASPI manager (chaining)
prev_entry      dd      0
prev_count      db      0               ; its host adapters = our HA number

; ASPI call state
in_aspi         db      0
old_ss          dw      0
old_sp          dw      0
srb_ptr         dd      0
post_ptr        dd      0

; SCSI request state
s_dir           db      0               ; SRB direction: 0 any, 1 in, 2 out
s_len           dd      0               ; data length from the SRB
s_xfer          dd      0               ; x_xfer / x_over of the request
s_over          db      0
s_err           db      0               ; x_err of the request

;=============================================================================
; ASPI entry point
;=============================================================================
aspi_entry:
        push    bp
        mov     bp, sp
        pushf
        pushad
        push    ds
        push    es
        push    fs
        push    gs
        ; Windows 3.x enhanced mode: no switch to another VM while we are
        ; in here (one driver state, one internal stack for all DOS boxes)
        xor     di, di
        smsw    ax
        test    al, 1
        jz      .nocs
        mov     ax, 1681h               ; begin critical section
        int     2Fh
        inc     di
.nocs:  push    di                      ; (kept on the caller's stack)
        mov     ax, cs
        mov     ds, ax
        les     si, [ss:bp+6]
        mov     [srb_ptr], si
        mov     [srb_ptr+2], es

        ; adapters of the previous manager: pass the request on, on the
        ; caller's stack (other managers may need more than our stack)
        mov     al, [es:si+SRB_HaId]
        cmp     al, [prev_count]
        jae     .ours
        push    es
        push    si
        call    far [cs:prev_entry]
        add     sp, 4
        mov     ax, cs
        mov     ds, ax
        les     si, [ss:bp+6]
        cmp     byte [es:si+SRB_Cmd], SC_HA_INQUIRY
        jne     .restore
        inc     byte [es:si+HA_Count]   ; count our adapter as well
        jmp     .restore

.ours:  cmp     byte [in_aspi], 0
        jne     .nested
        pushf
        pop     dx
        cli
        mov     [old_ss], ss
        mov     [old_sp], sp
        mov     ax, cs
        mov     ss, ax
        mov     sp, stack_top
        push    dx
        popf
.nested:
        inc     byte [in_aspi]
        cld
        call    aspi_dispatch
        dec     byte [in_aspi]
        jnz     .restore
        cli
        mov     ss, [old_ss]
        mov     sp, [old_sp]
.restore:
        pop     cx                      ; critical section to end?
        jcxz    .nocs2
        mov     ax, 1682h               ; end critical section
        int     2Fh
.nocs2: pop     gs
        pop     fs
        pop     es
        pop     ds
        popad
        popf
        pop     bp
        retf

aspi_dispatch:
        les     si, [srb_ptr]
        mov     al, [es:si+SRB_Cmd]
        mov     ah, [es:si+SRB_HaId]
        cmp     al, SC_HA_INQUIRY
        je      srb_ha_inquiry
        cmp     ah, [prev_count]
        jne     .bad_ha
        cmp     al, SC_GET_DEV_TYPE
        je      srb_get_dev_type
        cmp     al, SC_EXEC_SCSI_CMD
        je      srb_exec
        cmp     al, SC_ABORT_SRB
        je      srb_abort
        cmp     al, SC_RESET_DEV
        je      srb_reset
        cmp     al, SC_GET_DISK_INFO
        je      srb_disk_info
        mov     byte [es:si+SRB_Status], SS_INVALID_CMD
        ret
.bad_ha:
        mov     byte [es:si+SRB_Status], SS_INVALID_HA
        ret

;--- SC_HA_INQUIRY -----------------------------------------------------------
srb_ha_inquiry:
        mov     al, [prev_count]
        inc     al
        mov     [es:si+HA_Count], al
        dec     al
        cmp     [es:si+SRB_HaId], al
        je      .ok
        mov     byte [es:si+SRB_Status], SS_INVALID_HA
        ret
.ok:    mov     byte [es:si+HA_SCSI_ID], HA_TARGET_ID
        lea     di, [si+HA_ManagerId]
        push    si
        mov     si, mgr_id
        mov     cx, 48                  ; manager id, adapter id, unique
        rep     movsb
        pop     si
        cmp     word [es:si+SRB_Hdr_Rsvd], 0AA55h ; extended inquiry?
        jne     .done
        mov     word [es:si+SRB_Hdr_Rsvd], 55AAh
        mov     al, [es:si+SRB_Hdr_Rsvd+2]      ; bytes requested
        cmp     al, 2
        jb      .none
        mov     word [es:si+HA_ExtFlags], HA_EXT_RESIDUAL
        mov     al, 2
        jmp     .ext
.none:  xor     al, al
.ext:   mov     [es:si+SRB_Hdr_Rsvd+2], al
.done:  mov     byte [es:si+SRB_Status], SS_COMP
        ret

; ES:SI = SRB -> BX = SCSI ID.  CF (and status set) if no device.
find_dev:
        movzx   bx, byte [es:si+SRB_Target]
        cmp     bl, NTARGET
        jae     .none
        cmp     byte [dev_type+bx], 0FFh
        je      .none
        cmp     byte [es:si+SRB_Lun], 0
        jne     .none
        clc
        ret
.none:  mov     byte [es:si+SRB_Status], SS_NO_DEVICE
        stc
        ret

;--- SC_GET_DEV_TYPE ---------------------------------------------------------
srb_get_dev_type:
        call    find_dev
        jc      .out
        mov     al, [dev_type+bx]
        mov     [es:si+SRB_DeviceType], al
        mov     byte [es:si+SRB_Status], SS_COMP
.out:   ret

;--- SC_ABORT_SRB: all requests complete synchronously -----------------------
srb_abort:
        mov     byte [es:si+SRB_Status], SS_ABORT_FAIL
        ret

;--- SC_GET_DISK_INFO --------------------------------------------------------
srb_disk_info:
        call    find_dev
        jc      .out
        xor     ax, ax
        mov     [es:si+SRB_DriveFlags], ax      ; not an INT 13h drive
        mov     [es:si+SRB_Heads], ax
        mov     byte [es:si+SRB_Status], SS_COMP
.out:   ret

;--- SC_RESET_DEV: a SCSI bus reset behind the port --------------------------
srb_reset:
        call    find_dev
        jc      .out
        call    dev_bus_reset
        les     si, [srb_ptr]
        mov     word [es:si+SRB_HaStat], 0      ; HaStat, TargStat
        mov     byte [es:si+SRB_Status], SS_COMP
        jmp     call_post
.out:   ret

; call the post routine of the SRB at ES:SI if requested
call_post:
        test    byte [es:si+SRB_Flags], SRB_POSTING
        jz      .out
        mov     eax, [es:si+SRB_PostProc]
        or      eax, eax
        jz      .out
        mov     [post_ptr], eax
        sti
        push    es
        push    si
        call    far [post_ptr]
        add     sp, 4
.out:   ret

;--- SC_EXEC_SCSI_CMD --------------------------------------------------------
srb_exec:
        call    find_dev
        jnc     .dev
        jmp     call_post
.dev:   mov     [x_target], bl
        mov     word [es:si+SRB_HaStat], 0      ; HaStat, TargStat
        test    byte [es:si+SRB_Flags], SRB_LINKING
        jnz     .invalid
        movzx   cx, byte [es:si+SRB_CDBLen]
        or      cx, cx
        jz      .invalid
        cmp     cx, 16
        ja      .invalid
        mov     [cdb_len], cl
        xor     eax, eax                ; ATAPI packets are 12 bytes
        mov     [cdb_buf], eax
        mov     [cdb_buf+4], eax
        mov     [cdb_buf+8], eax
        mov     [cdb_buf+12], eax
        push    si
        add     si, SRB_CDB
        mov     di, cdb_buf
.copy:  mov     al, [es:si]
        mov     [di], al
        inc     si
        inc     di
        loop    .copy
        pop     si

        ; the drive tells the direction with its bus phase; an explicit
        ; SRB direction is only checked against it
        mov     eax, [es:si+SRB_BufLen]
        mov     [s_len], eax
        mov     [x_len], eax
        mov     eax, [es:si+SRB_BufPointer]
        mov     [x_buf], eax
        mov     [s_buf], eax
        mov     al, [es:si+SRB_Flags]
        and     al, SRB_DIR_MASK
        xor     ah, ah
        cmp     al, SRB_DIR_IN
        jne     .notin
        mov     ah, 1
.notin: cmp     al, SRB_DIR_OUT
        jne     .notout
        mov     ah, 2
.notout:
        cmp     al, SRB_DIR_NONE
        jne     .dirset
        mov     dword [x_len], 0
.dirset:
        mov     [s_dir], ah
        mov     byte [s_xlate], XL_NONE
        cmp     byte [lpt_chip], CHIP_EPAT
        jb      .go
        cmp     byte [dev_ata], 0
        jne     .go
        cmp     byte [dev_fdd], 0
        jne     .go
        call    translate_cdb           ; ATAPI drive
        jnc     .xl
        jmp     .invalid
.xl:    cmp     byte [s_xlate], XL_NONE
        je      .go
        mov     word [x_buf], bounce
        mov     [x_buf+2], cs
        movzx   eax, word [s_blen]
        mov     [x_len], eax
.go:    mov     al, [s_dir]
        mov     [x_dir], al
        call    dev_exec
        les     si, [srb_ptr]
        push    eax
        mov     eax, [x_xfer]
        mov     [s_xfer], eax
        mov     ah, [x_over]
        mov     [s_over], ah
        mov     ah, [x_err]
        mov     [s_err], ah
        pop     eax
        cmp     al, XR_OK
        jne     .hba_err
        mov     al, [x_status]
        and     al, 3Eh
        mov     [es:si+SRB_TargStat], al
        jz      .good
        cmp     al, STATUS_CHKCOND
        jne     .err
        ; ATAPI drives cannot change their block size and reject a MODE
        ; SELECT that only carries a block descriptor (ASPICD.SYS sends one
        ; to set 2048 byte blocks).  Report success, as ATAPI managers do.
        cmp     byte [s_xlate], XL_MODESELECT
        jne     .check
        cmp     byte [s_msel_bdonly], 0
        je      .check
        mov     al, [s_err]
        shr     al, 4
        cmp     al, SENSE_ILLEGAL_REQUEST
        jne     .check
        mov     byte [es:si+SRB_TargStat], 0
        jmp     .good
.check: call    auto_sense
        les     si, [srb_ptr]
        jmp     .err
.good:  cmp     byte [s_xlate], XL_MODESENSE
        jne     .nomsb
        call    modesense_back
.nomsb: cmp     byte [lpt_chip], CHIP_EPAT
        jb      .noinq
        call    inquiry_fixup
.noinq: mov     byte [es:si+SRB_Status], SS_COMP
        call    exec_extras
        jmp     call_post
.hba_err:
        mov     byte [es:si+SRB_HaStat], HASTAT_SEL_TO
        cmp     al, XR_HBA
        jne     .err
        mov     byte [es:si+SRB_HaStat], HASTAT_PHASE_ERR
.err:   mov     byte [es:si+SRB_Status], SS_ERR
        call    exec_extras
        jmp     call_post
.invalid:
        les     si, [srb_ptr]
        mov     byte [es:si+SRB_Status], SS_INVALID_CMD
        jmp     call_post

; Data overrun reporting and residual byte count for the SRB at ES:SI
;  - overrun (the drive wanted to move more data than the buffer holds, or
;    in the other direction) is reported as HaStat 12h when the SRB gives
;    an explicit direction
;  - with SRB_RESIDUAL, SRB_BufLen returns the bytes not transferred
exec_extras:
        cmp     byte [s_over], 0
        je      .resid
        cmp     byte [s_dir], 0
        je      .resid
        cmp     byte [es:si+SRB_HaStat], HASTAT_OK
        jne     .resid
        mov     byte [es:si+SRB_HaStat], HASTAT_DO_DU
        mov     byte [es:si+SRB_Status], SS_ERR
.resid: test    byte [es:si+SRB_Flags], SRB_RESIDUAL
        jz      .out
        xor     eax, eax
        cmp     byte [s_xlate], XL_NONE ; translated: buffer sizes differ
        jne     .set
        mov     eax, [s_len]
        sub     eax, [s_xfer]
        jae     .set
        xor     eax, eax
.set:   mov     [es:si+SRB_BufLen], eax
.out:   ret

;-----------------------------------------------------------------------------
; Automatic REQUEST SENSE after CHECK CONDITION for the SRB at [srb_ptr]:
; the sense data go straight into the sense area of the SRB
;-----------------------------------------------------------------------------
auto_sense:
        les     si, [srb_ptr]
        movzx   cx, byte [es:si+SRB_SenseLen]
        jcxz    .out
        movzx   di, byte [es:si+SRB_CDBLen]
        add     di, si
        add     di, SRB_CDB
        push    di
        push    cx
        xor     al, al
        rep     stosb
        pop     cx
        pop     di
        mov     [x_buf], di
        mov     [x_buf+2], es
        movzx   ecx, cx
        mov     [x_len], ecx
        xor     eax, eax
        mov     [cdb_buf], eax
        mov     [cdb_buf+4], eax
        mov     byte [cdb_buf], SCSI_REQUEST_SENSE
        mov     [cdb_buf+4], cl
        mov     byte [cdb_len], 6
        mov     byte [x_dir], 1
        push    di
        call    dev_exec
        pop     di
        les     si, [srb_ptr]
        cmp     al, XR_OK
        jne     .fake
        test    byte [x_status], 3Eh
        jz      .out
.fake:  ; no sense data: a fixed format record with the sense key of an
        ; ATAPI drive's error register, if there is one
        mov     byte [es:di], 70h
        cmp     byte [es:si+SRB_SenseLen], 3
        jb      .out
        mov     al, [s_err]
        shr     al, 4
        mov     [es:di+2], al
        cmp     byte [es:si+SRB_SenseLen], 8
        jb      .out
        mov     byte [es:di+7], 0Ah
.out:   ret

%include "lpt.inc"
%include "ppa.inc"
%include "imm.inc"
%include "epat.inc"
%include "bpck.inc"
%include "on26.inc"
%include "epia.inc"
%include "fc.inc"
%include "ata.inc"
%include "fdc.inc"
%include "atapi.inc"

;-----------------------------------------------------------------------------
; Stack
;-----------------------------------------------------------------------------
                align   16
stack_bottom:   times STACK_SIZE db 0
stack_top:

resident_end:

;=============================================================================
; Initialisation (discarded after init)
;=============================================================================

opt_port        dw      0               ; /P:
opt_mode        db      0FFh            ; /M:, 0FFh: choose
opt_quiet       db      0               ; /Q
opt_verbose     db      0               ; /V
inq_buf         times 36 db 0
id_buf          times 512 db 0         ; IDENTIFY DEVICE data

msg_banner      db      'PORTASPI ', VERSION_STR
                db      ' - ASPI manager for parallel port devices', 13, 10, 0
msg_no386       db      'PORTASPI needs a 386 or better', 13, 10, 0
msg_no_dev      db      'PORTASPI: no drive found on the parallel ports', 13, 10, 0
msg_not_inst    db      'PORTASPI not installed', 13, 10, 0
msg_bad_opt     db      'PORTASPI: unknown option ignored', 13, 10, 0
msg_port        db      '  port ', 0
msg_ppa         db      'h: Iomega PPA, ', 0
msg_imm         db      'h: Iomega IMM, ', 0
msg_epat        db      'h: Shuttle EPAT, ', 0
msg_bpck        db      'h: Micro Solutions BackPack, ', 0
msg_on26        db      'h: OnSpec 90c26, ', 0
msg_epia        db      'h: Shuttle EPIA, ', 0
msg_frpw        db      'h: Freecom Power, ', 0
msg_friq        db      'h: Freecom IQ, ', 0
msg_mode_nib    db      'nibble mode', 13, 10, 0
msg_mode_ps2    db      'PS/2 mode', 13, 10, 0
msg_mode_epp    db      'EPP mode', 13, 10, 0
msg_target      db      '  target ', 0
msg_colon       db      ': ', 0
msg_resident    db      '  resident: ', 0
msg_bytes       db      ' bytes at segment ', 0
msg_chain1      db      '  chained to ', 0
msg_chain2      db      ' (adapters 0-', 0
msg_chain3      db      '), this is adapter ', 0
msg_crlf        db      13, 10, 0

init:
        pushad
        push    ds
        push    es
        push    fs
        push    gs
        mov     ax, cs
        mov     ds, ax
        mov     es, ax
        cld

        call    parse_cmdline           ; may set /Q before the banner
        cmp     byte [opt_quiet], 0
        jne     .nobanner
        mov     si, msg_banner
        call    print
.nobanner:
        call    check_386
        jnc     .cpu_ok
        mov     si, msg_no386
        jmp     init_fail
.cpu_ok:
        call    find_prev_manager

        ; the port of /P:, or the LPT ports of the BIOS data area
        mov     ax, [opt_port]
        or      ax, ax
        jz      .bda
        call    try_port
        jnc     .found
        jmp     .nodev
.bda:   xor     bx, bx
.next:  push    es
        push    word 40h
        pop     es
        mov     ax, [es:8+bx]
        pop     es
        or      ax, ax
        jz      .skip
        push    bx
        call    try_port
        pop     bx
        jnc     .found
.skip:  add     bx, 2
        cmp     bx, 6
        jb      .next
.nodev: mov     si, msg_no_dev
        jmp     init_fail

.found: cmp     byte [dev_fdd], 0       ; the floppy motor goes off on
        je      .notimer                ; the timer tick
        push    es
        mov     ax, 351Ch
        int     21h
        mov     [fd_old1c], bx
        mov     [fd_old1c+2], es
        pop     es
        mov     dx, fd_timer
        mov     ax, 251Ch
        int     21h
.notimer:
        les     bx, [req_ptr]
        mov     word [es:bx+RQ_END], resident_end
        cmp     byte [opt_verbose], 0
        je      .sized
        mov     si, msg_resident
        call    print
        mov     ax, resident_end
        call    print_dec
        mov     si, msg_bytes
        call    print
        mov     ax, cs
        call    print_hex16
        mov     si, msg_crlf
        call    print
        les     bx, [req_ptr]
.sized:
        mov     [es:bx+RQ_END+2], cs
        mov     word [es:bx+RQ_STATUS], 0100h
        jmp     init_out

; SI = error message
init_fail:
        call    print
        mov     si, msg_not_inst
        call    print
        les     bx, [req_ptr]
        mov     word [es:bx+RQ_END], 0
        mov     [es:bx+RQ_END+2], cs
        mov     word [es:bx+RQ_STATUS], 810Ch
init_out:
        pop     gs
        pop     fs
        pop     es
        pop     ds
        popad
        ret

;-----------------------------------------------------------------------------
; Look for a PPA chip with drives on port AX.  CF if there is none.
;-----------------------------------------------------------------------------
try_port:
        mov     [lpt_base], ax
        call    port_setup              ; mode, ECR
        mov     byte [lpt_chip], CHIP_IMM
        call    imm_init                ; the IMM answers command packets
        jnc     .chip
        mov     byte [lpt_chip], CHIP_EPAT
        mov     al, [lpt_mode]
        push    ax
        mov     byte [lpt_mode], MODE_NIBBLE    ; the only EPAT mode so far
        call    epat_init               ; a drive behind a Shuttle EPAT
        pop     ax
        jnc     .chip
        mov     [lpt_mode], al
.ppa:   mov     byte [lpt_chip], CHIP_BPCK
        call    bpck_init               ; an ATA or ATAPI drive behind a BackPack
        jc      .ppa2
        cmp     byte [lpt_mode], MODE_EPP       ; nibble or PS/2 so far
        jne     .chip
        mov     byte [lpt_mode], MODE_NIBBLE
        jmp     .chip
.ppa2:  call    on26_init               ; an ATA or ATAPI drive behind an OnSpec
        jc      .ppa3
        cmp     byte [lpt_mode], MODE_EPP       ; nibble or 8-bit so far
        jne     .chip
        mov     byte [lpt_mode], MODE_NIBBLE
        jmp     .chip
.ppa3:  call    epia_init               ; an ATA or ATAPI drive behind an EPIA
        jc      .ppa5
        cmp     byte [lpt_mode], MODE_EPP       ; nibble or 8-bit so far
        jne     .chip
        mov     byte [lpt_mode], MODE_NIBBLE
        jmp     .chip
.ppa5:  mov     al, [lpt_mode]          ; a drive behind a Freecom bridge
        cmp     al, MODE_EPP            ; nibble or 8-bit so far
        jne     .fc
        mov     byte [lpt_mode], MODE_NIBBLE
.fc:    push    ax
        call    fc_init
        pop     ax
        jnc     .chip
        mov     [lpt_mode], al
.ppa4:  mov     byte [lpt_chip], CHIP_PPA
        call    ppa_init
        jc      .none
.chip:
        xor     bx, bx                  ; INQUIRY on every SCSI ID
        xor     cx, cx                  ; drives found
.id:    mov     byte [dev_type+bx], 0FFh
        call    inquire
        jc      .noid
        mov     al, [inq_buf]
        cmp     al, 7Fh                 ; no device at this LUN
        je      .noid
        and     al, 1Fh
        mov     [dev_type+bx], al
        inc     cx
.noid:  inc     bx
        cmp     bx, NTARGET
        jb      .id
        jcxz    .none
        call    set_ident
        cmp     byte [opt_quiet], 0
        jne     .quiet
        call    print_port
        xor     bx, bx
.pr:    cmp     byte [dev_type+bx], 0FFh
        je      .prn
        call    inquire                 ; again, for the names
        call    print_dev
.prn:   inc     bx
        cmp     bx, NTARGET
        jb      .pr
.quiet: clc
        ret
.none:  stc
        ret

; INQUIRY (36 bytes) on SCSI ID BX into inq_buf.  CF on failure.
inquire:
        push    bx
        push    cx
        mov     [x_target], bl
        mov     word [x_buf], inq_buf
        mov     [x_buf+2], cs
        mov     dword [x_len], 36
        mov     byte [x_dir], 1
        xor     eax, eax
        mov     [cdb_buf], eax
        mov     [cdb_buf+4], eax
        mov     byte [cdb_buf], SCSI_INQUIRY
        mov     byte [cdb_buf+4], 36
        mov     byte [cdb_len], 6
        call    dev_exec
        pop     cx
        pop     bx
        cmp     al, XR_OK
        jne     .fail
        test    byte [x_status], 3Eh
        jnz     .fail
        cmp     dword [x_xfer], 5
        jb      .fail
        clc
        ret
.fail:  stc
        ret

; the adapter name: "chip port mode", for example "EPAT 0378 nibble"
set_ident:
        push    cs
        pop     es
        mov     di, ha_ident
        mov     cx, 16
        mov     al, ' '
        rep     stosb
        mov     di, ha_ident
        movzx   si, byte [lpt_chip]
        shl     si, 1
        mov     si, [id_chips+si]
.name:  lodsb
        or      al, al
        jz      .port
        stosb
        jmp     .name
.port:  inc     di
        mov     ax, [lpt_base]
        mov     cx, 4
.hex:   rol     ax, 4
        push    ax
        and     al, 0Fh
        add     al, '0'
        cmp     al, '9'
        jbe     .d
        add     al, 7
.d:     stosb
        pop     ax
        loop    .hex
        inc     di
        mov     si, id_nibble
        cmp     byte [lpt_mode], MODE_PS2
        jne     .n1
        mov     si, id_ps2
.n1:    cmp     byte [lpt_mode], MODE_EPP
        jne     .n2
        mov     si, id_epp
.n2:    mov     cx, ha_ident + 16
        sub     cx, di
.m:     lodsb
        or      al, al
        jz      .out
        stosb
        loop    .m
.out:   ret

id_chips        dw      id_ppa, id_imm, id_epat, id_bpck, id_on26, id_epia, id_frpw, id_friq
id_ppa          db      'PPA', 0
id_imm          db      'IMM', 0
id_epat         db      'EPAT', 0
id_bpck         db      'BPCK', 0
id_on26         db      'ON26', 0
id_epia         db      'EPIA', 0
id_frpw         db      'FRPW', 0
id_friq         db      'FRIQ', 0
id_nibble       db      'nibble', 0
id_ps2          db      'PS/2', 0
id_epp          db      'EPP', 0

print_port:
        mov     si, msg_port
        call    print
        mov     ax, [lpt_base]
        call    print_hex16
        mov     si, msg_ppa
        cmp     byte [lpt_chip], CHIP_IMM
        jne     .chip
        mov     si, msg_imm
.chip:  cmp     byte [lpt_chip], CHIP_EPAT
        jne     .chip2
        mov     si, msg_epat
.chip2: cmp     byte [lpt_chip], CHIP_BPCK
        jne     .chip3
        mov     si, msg_bpck
.chip3: cmp     byte [lpt_chip], CHIP_ON26
        jne     .chip4
        mov     si, msg_on26
.chip4: cmp     byte [lpt_chip], CHIP_EPIA
        jne     .chip5
        mov     si, msg_epia
.chip5: cmp     byte [lpt_chip], CHIP_FRPW
        jne     .chip6
        mov     si, msg_frpw
.chip6: cmp     byte [lpt_chip], CHIP_FRIQ
        jne     .chip7
        mov     si, msg_friq
.chip7: call    print
        mov     si, msg_mode_nib
        cmp     byte [lpt_mode], MODE_PS2
        jne     .n1
        mov     si, msg_mode_ps2
.n1:    cmp     byte [lpt_mode], MODE_EPP
        jne     .n2
        mov     si, msg_mode_epp
.n2:    jmp     print

; "  target n: vendor product revision" from inq_buf
print_dev:
        mov     si, msg_target
        call    print
        mov     al, bl
        add     al, '0'
        call    putc
        mov     si, msg_colon
        call    print
        mov     si, inq_buf+8
        mov     cx, 28
.c:     lodsb
        cmp     al, ' '
        jae     .p
        mov     al, ' '
.p:     call    putc
        loop    .c
        mov     si, msg_crlf
        jmp     print

putc:   push    ax
        push    dx
        mov     dl, al
        mov     ah, 2
        int     21h
        pop     dx
        pop     ax
        ret

print:  push    ax
        push    si
.l:     mov     al, [si]
        or      al, al
        jz      .e
        call    putc
        inc     si
        jmp     .l
.e:     pop     si
        pop     ax
        ret

print_hex16:
        push    ax
        mov     al, ah
        call    print_hex8
        pop     ax
print_hex8:
        push    ax
        shr     al, 4
        call    .nib
        pop     ax
        push    ax
        and     al, 0Fh
        call    .nib
        pop     ax
        ret
.nib:   add     al, '0'
        cmp     al, '9'
        jbe     .p
        add     al, 7
.p:     jmp     putc

print_dec:                              ; AX
        push    ax
        push    bx
        push    cx
        push    dx
        mov     bx, 10
        xor     cx, cx
.div:   xor     dx, dx
        div     bx
        push    dx
        inc     cx
        or      ax, ax
        jnz     .div
.out:   pop     ax
        add     al, '0'
        call    putc
        loop    .out
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; CF if the CPU is older than a 386
check_386:
        pushf
        pushf
        pop     ax
        and     ax, 0FFFh
        push    ax
        popf
        pushf
        pop     ax
        and     ax, 0F000h
        cmp     ax, 0F000h              ; 8086/88: bits 12-15 always set
        je      .old
        pushf
        pop     ax
        or      ax, 7000h
        push    ax
        popf
        pushf
        pop     ax
        test    ax, 7000h               ; 286 real mode: bits 12-14 clear
        jz      .old
        popf
        clc
        ret
.old:   popf
        stc
        ret

;-----------------------------------------------------------------------------
; Command line parsing
;-----------------------------------------------------------------------------
parse_cmdline:
        push    es
        les     bx, [req_ptr]
        les     si, [es:bx+RQ_CMDLINE]
.skipname:                              ; skip the driver file name
        mov     al, [es:si]
        call    is_eol
        je      .done
        cmp     al, ' '
        jbe     .args
        cmp     al, '/'
        je      .args
        inc     si
        jmp     .skipname
.args:  mov     al, [es:si]
        call    is_eol
        je      .done
        inc     si
        cmp     al, '/'
        jne     .args
        mov     al, [es:si]
        call    upcase
        inc     si
        cmp     al, 'P'
        je      .opt_p
        cmp     al, 'M'
        je      .opt_m
        cmp     al, 'T'
        je      .opt_t
        cmp     al, 'Q'
        je      .opt_q
        cmp     al, 'V'
        je      .opt_v
        cmp     al, 'I'
        je      .opt_i
.bad:   push    si
        mov     si, msg_bad_opt
        call    print
        pop     si
        jmp     .args

.opt_p: call    get_colon
        jc      .bad
        call    get_hex
        jc      .bad
        or      ax, ax
        jz      .bad
        mov     [opt_port], ax
        jmp     .args
.opt_t: call    get_colon
        jc      .bad
        call    get_number
        jc      .bad
        or      ax, ax
        jz      .bad
        mov     cx, 182
        mul     cx
        mov     cx, 10
        div     cx
        movzx   eax, ax
        mov     [tmo_ticks], eax
        jmp     .args
.opt_q: mov     byte [opt_quiet], 1
        jmp     .args
.opt_v: mov     byte [opt_verbose], 1
        jmp     .args
.opt_m: call    get_colon
        jc      .bad
        mov     di, kw_nibble
        mov     ah, MODE_NIBBLE
        call    match_kw
        jnc     .setm
        mov     di, kw_ps2
        mov     ah, MODE_PS2
        call    match_kw
        jnc     .setm
        mov     di, kw_epp
        mov     ah, MODE_EPP
        call    match_kw
        jc      .bad
.setm:  mov     [opt_mode], ah
        jmp     .args
.opt_i: mov     al, [es:si]             ; /ID:name
        call    upcase
        cmp     al, 'D'
        jne     .bad
        inc     si
        call    get_colon
        jc      .bad
        mov     di, mgr_id
        mov     cx, 16
.idfill:
        mov     al, [es:si]
        call    is_eol
        je      .idpad
        cmp     al, ' '
        jbe     .idpad
        cmp     al, '/'
        je      .idpad
        mov     [di], al
        inc     di
        inc     si
        loop    .idfill
        jmp     .args
.idpad: mov     byte [di], ' '
        inc     di
        loop    .idpad
        jmp     .args
.done:  pop     es
        ret

; compare the rest of an option at ES:SI with the keyword at DI (upper case,
; zero terminated).  Advances SI on a match, CF otherwise.  Keeps AH.
match_kw:
        push    si
        push    ax
.c:     mov     ah, [di]
        or      ah, ah
        jz      .ok
        mov     al, [es:si]
        call    upcase
        cmp     al, ah
        jne     .no
        inc     si
        inc     di
        jmp     .c
.ok:    pop     ax
        add     sp, 2
        clc
        ret
.no:    pop     ax
        pop     si
        stc
        ret

kw_nibble       db      'NIBBLE', 0
kw_ps2          db      'PS2', 0
kw_epp          db      'EPP', 0

is_eol: cmp     al, 0
        je      .y
        cmp     al, 0Dh
        je      .y
        cmp     al, 0Ah
.y:     ret

upcase: cmp     al, 'a'
        jb      .n
        cmp     al, 'z'
        ja      .n
        sub     al, 20h
.n:     ret

get_colon:
        cmp     byte [es:si], ':'
        jne     .no
        inc     si
        clc
        ret
.no:    stc
        ret

; decimal number at ES:SI -> AX.  CF if none.
get_number:
        xor     ax, ax
        xor     cx, cx
.dig:   mov     dl, [es:si]
        sub     dl, '0'
        cmp     dl, 9
        ja      .end
        mov     dh, 0
        imul    ax, ax, 10
        add     ax, dx
        inc     si
        inc     cx
        jmp     .dig
.end:   cmp     cx, 1
        ret                             ; CF if cx == 0

; hexadecimal number at ES:SI -> AX.  CF if none.
get_hex:
        xor     ax, ax
        xor     cx, cx
.dig:   mov     dl, [es:si]
        cmp     dl, 'a'
        jb      .uc
        sub     dl, 20h
.uc:    sub     dl, '0'
        cmp     dl, 9
        jbe     .add
        sub     dl, 'A' - '0' - 10
        cmp     dl, 10
        jb      .end
        cmp     dl, 15
        ja      .end
.add:   shl     ax, 4
        or      al, dl
        inc     si
        inc     cx
        jmp     .dig
.end:   cmp     cx, 1
        ret

;-----------------------------------------------------------------------------
; Look for an ASPI manager loaded before us and chain to it
;-----------------------------------------------------------------------------
find_prev_manager:
        mov     byte [hdr_name+7], '#'  ; DOS must not find our own header
        mov     ax, 3D00h
        mov     dx, prev_name
        int     21h
        mov     byte [hdr_name+7], '$'
        jc      .none
        mov     bx, ax
        mov     ax, 4402h               ; IOCTL read: entry point
        mov     dx, prev_entry
        mov     cx, 4
        int     21h
        pushf
        mov     ah, 3Eh
        int     21h
        popf
        jc      .none
        cmp     dword [prev_entry], 0
        je      .none
        mov     di, prev_srb            ; host adapter inquiry, adapter 0
        mov     cx, 64
        xor     al, al
        rep     stosb
        push    cs
        push    word prev_srb
        call    far [prev_entry]
        add     sp, 4
        mov     cx, 0FFFFh              ; managers that complete later
.wait:  cmp     byte [prev_srb+SRB_Status], SS_PENDING
        jne     .done
        loop    .wait
.done:  cmp     byte [prev_srb+SRB_Status], SS_COMP
        jne     .bad
        mov     al, [prev_srb+HA_Count]
        or      al, al
        jz      .bad
        mov     [prev_count], al
        cmp     byte [opt_quiet], 0
        jne     .out
        mov     si, msg_chain1
        call    print
        mov     cx, 16
        mov     si, prev_srb+HA_ManagerId
.name:  lodsb
        call    putc
        loop    .name
        mov     si, msg_chain2
        call    print
        movzx   ax, byte [prev_count]
        dec     ax
        call    print_dec
        mov     si, msg_chain3
        call    print
        movzx   ax, byte [prev_count]
        call    print_dec
        mov     si, msg_crlf
        call    print
.out:   ret
.bad:   mov     dword [prev_entry], 0
.none:  mov     byte [prev_count], 0
        ret

prev_name       db      'SCSIMGR$', 0
prev_srb        times 64 db 0

;-----------------------------------------------------------------------------
; Port mode: ECR of an ECP port, bidirectional test
;-----------------------------------------------------------------------------
port_setup:
        mov     dx, [lpt_base]
        call    ecr_present
        mov     [lpt_ecr], al
        mov     al, [opt_mode]
        cmp     al, 0FFh
        jne     .set
        mov     al, MODE_PS2            ; PS/2 where the port can read bytes
        cmp     byte [lpt_ecr], 0
        jne     .set
        call    bidir_test
        mov     al, MODE_PS2
        jnc     .set
        mov     al, MODE_NIBBLE
.set:   mov     [lpt_mode], al
        cmp     byte [lpt_ecr], 0
        je      .out
        ; an ECP port in the mode for it: 001 (PS/2) or 100 (EPP)
        mov     ah, 34h
        cmp     al, MODE_EPP
        jne     .ecr
        mov     ah, 94h
.ecr:   mov     al, ah
        add     dx, 402h
        out     dx, al
        sub     dx, 402h
.out:   ret

; AL = 1 if the port at DX has an ECR (as Linux parport_ECR_present)
ecr_present:
        add     dx, 2
        in      al, dx                  ; control
        mov     ah, al
        add     dx, 400h
        in      al, dx                  ; ECR (base + 402h)
        sub     dx, 400h
        and     al, 3
        mov     bl, ah
        and     bl, 3
        cmp     al, bl
        jne     .chk
        mov     al, ah                  ; the same bits: toggle control bit 1
        xor     al, 2
        out     dx, al
        in      al, dx
        mov     ah, al
        add     dx, 400h
        in      al, dx
        sub     dx, 400h
        and     al, 2
        and     ah, 2
        cmp     al, ah
        je      .no
.chk:   add     dx, 400h
        in      al, dx
        and     al, 3
        cmp     al, 1                   ; FIFO empty, not full
        jne     .no4
        mov     al, 34h
        out     dx, al
        in      al, dx
        sub     dx, 400h
        cmp     al, 35h
        jne     .no
        mov     al, 0Ch
        out     dx, al
        sub     dx, 2
        mov     al, 1
        ret
.no4:   sub     dx, 400h
.no:    mov     al, 0Ch
        out     dx, al
        sub     dx, 2
        xor     al, al
        ret

; CF if the port at DX cannot read bytes back (data lines output only)
bidir_test:
        add     dx, 2
        mov     al, 2Ch                 ; reverse direction
        out     dx, al
        sub     dx, 2
        mov     al, 55h
        out     dx, al
        in      al, dx
        mov     ah, al
        mov     al, 0AAh
        out     dx, al
        in      al, dx
        add     dx, 2
        push    ax
        mov     al, 0Ch
        out     dx, al
        pop     ax
        sub     dx, 2
        cmp     ax, 55AAh               ; AH = 55h, AL = 0AAh: latched
        je      .no
        clc
        ret
.no:    stc
        ret
