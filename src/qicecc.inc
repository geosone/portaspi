;-----------------------------------------------------------------------------
; The Reed-Solomon code of a floppy tape segment (QIC-80, QIC-3010/3020)
;
; Each byte column of a segment's n good sectors is a code word over
; GF(256) (x^8+x^7+x^2+x+1) whose generator has the roots a^-1, 1 and a:
; good sector i is the coefficient of x^(n-1-i), the last three carry the
; parity.  The syndromes S0, S1 and S-1 of a whole column are zero.  An
; error e in sector i, X = a^(n-1-i), adds e to S0, e * X to S1 and e / X
; to S-1.
;
; Without erasures: one damaged sector, read without a CRC error but
; wrong, gives in each column it touches e = S0 at X = S1 / S0, with
; S-1 = S0 / X; all columns must agree on i.
;
; Erasures are sectors the controller could not read (CRC errors): their
; places are known, so three syndromes put up to three of them right.
; With k of them the first k syndromes (S0, S1, S-1) give the errors:
; A e = S, A[r][j] = 1, X_j, 1 / X_j for row r = 0, 1, 2; A is inverted
; once a segment and applied to each column.  The syndromes left over
; must then agree, or there is more damage than the erasures.
;
; Writing, the parity of a column m_0 .. m_n-4 is the remainder of
; m(x) x^3 by g(x) = (x + a^-1)(x + 1)(x + a) = x^3 + c x^2 + c x + 1,
; c = a^-1 + 1 + a = C0h: qt_encode.
;
; Uses [qt_buf] (the segment's good sectors, one after the other) and
; [qt_bcap] (their data bytes), qt_nera and qt_era (the erasures, as
; places in the buffer).  Test tool: tests/qicecc.asm.
;-----------------------------------------------------------------------------

gf_log          times 256 db 0
gf_exp          times 512 db 0          ; twice: a sum of two logs needs no mod
qt_epos         dw      0               ; the damaged sector, FFFFh: none yet
qt_nera         db      0               ; erasures
qt_era          times 3 db 0            ; their places
qt_elx          times 3 db 0            ; log X of each
qt_ga           times 9 db 0            ; A, row by row
qt_gm           times 9 db 0            ; its inverse
qt_syn          times 3 db 0            ; S0, S1, S-1 of a column
qt_err          times 3 db 0            ; the errors of the erasures
gf_c0           times 256 db 0          ; v * C0h

qt_gf_init:
        push    ax
        push    bx
        push    cx
        xor     cx, cx                  ; i
        mov     al, 1                   ; a^i
.l:     movzx   bx, al
        mov     [gf_log+bx], cl
        mov     bx, cx
        mov     [gf_exp+bx], al
        mov     [gf_exp+bx+255], al
        shl     al, 1
        jnc     .n
        xor     al, 87h
.n:     inc     cx
        cmp     cx, 255
        jb      .l
        mov     al, [gf_exp]
        mov     [gf_exp+510], al
        xor     bx, bx                  ; v * C0h
.c:     mov     al, bl
        mov     ah, 0C0h
        call    gf_mul
        mov     [gf_c0+bx], al
        inc     bx
        cmp     bx, 256
        jb      .c
        pop     cx
        pop     bx
        pop     ax
        ret

; AL * AH -> AL
gf_mul: or      al, al
        jz      .o
        or      ah, ah
        jz      .z
        push    bx
        push    si
        movzx   bx, al
        movzx   si, byte [gf_log+bx]
        movzx   bx, ah
        movzx   bx, byte [gf_log+bx]
        add     bx, si
        mov     al, [gf_exp+bx]
        pop     si
        pop     bx
        ret
.z:     xor     al, al
.o:     ret

; 1 / AL -> AL (AL not 0)
gf_inv: push    bx
        movzx   bx, al
        movzx   bx, byte [gf_log+bx]
        neg     bx
        add     bx, 255
        mov     al, [gf_exp+bx]
        pop     bx
        ret

; check, and correct, the segment in the buffer: [qt_bcap] / 1024 + 3
; good sectors, [qt_nera] of them erased.  CF if it is damaged beyond that.
qt_ecc: pushad
        mov     bp, [qt_bcap]
        shr     bp, 10
        add     bp, 3                   ; n
        cmp     byte [qt_nera], 0
        je      .quick
        call    qt_era_setup
        jc      .bad
        jmp     .full
.quick: xor     di, di                  ; quick: S0 of every column
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
        cmp     byte [qt_nera], 0
        je      .one
        mov     [qt_syn], dx
        mov     [qt_syn+2], bl
        call    qt_era_col
        jc      .bad
        jmp     .next
.one:   or      dl, dl
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

; the erasures' A and its inverse.  BP = n.  CF if they do not fit.
qt_era_setup:
        movzx   cx, byte [qt_nera]
        cmp     cx, 3
        ja      .bad
        xor     si, si                  ; erasure j
.x:     movzx   ax, byte [qt_era+si]
        cmp     ax, bp
        jae     .bad
        mov     bx, bp                  ; log X = n - 1 - i
        dec     bx
        sub     bx, ax
        mov     [qt_elx+si], bl
        mov     byte [qt_ga+si], 1      ; row 0: 1
        mov     al, [gf_exp+bx]
        mov     [qt_ga+3+si], al        ; row 1: X
        neg     bx
        add     bx, 255
        mov     al, [gf_exp+bx]
        mov     [qt_ga+6+si], al        ; row 2: 1 / X
        inc     si
        cmp     si, cx
        jb      .x
        call    qt_ginv
        ret
.bad:   stc
        ret

; invert the top left CX x CX of qt_ga into qt_gm (Gauss-Jordan; the rest
; of both is left 0).  CF if it is singular.
qt_ginv:
        push    ax
        push    bx
        push    dx
        push    si
        push    di
        xor     bx, bx                  ; M = I, rows and columns past CX 0
.i:     mov     byte [qt_gm+bx], 0
        inc     bx
        cmp     bx, 9
        jb      .i
        xor     bx, bx
.i1:    cmp     bx, cx
        jae     .i2
        mov     si, bx
        imul    si, si, 4               ; 3 * r + r
        mov     byte [qt_gm+si], 1
        inc     bx
        jmp     .i1
.i2:    mov     dx, 3                   ; rows and columns past CX: 0 in A
        sub     dx, cx
        xor     bx, bx
.z:     cmp     bx, 9
        jae     .go
        mov     ax, bx
        push    dx
        xor     dx, dx
        mov     si, 3
        div     si                      ; AX = row, DX = column
        cmp     ax, cx
        jae     .zz
        cmp     dx, cx
        jb      .zn
.zz:    mov     byte [qt_ga+bx], 0
.zn:    pop     dx
        inc     bx
        jmp     .z
.go:    xor     dx, dx                  ; DL = column c
.c:     cmp     dl, cl
        jae     .ok
        mov     dh, dl                  ; the pivot row: from c on
.p:     cmp     dh, cl
        jae     .bad
        movzx   si, dh
        imul    si, si, 3
        movzx   bx, dl
        cmp     byte [qt_ga+si+bx], 0
        jne     .piv
        inc     dh
        jmp     .p
.piv:   cmp     dh, dl                  ; to row c
        je      .sc
        movzx   di, dl
        imul    di, di, 3
        mov     bx, 3
.sw:    mov     al, [qt_ga+si]
        xchg    al, [qt_ga+di]
        mov     [qt_ga+si], al
        mov     al, [qt_gm+si]
        xchg    al, [qt_gm+di]
        mov     [qt_gm+si], al
        inc     si
        inc     di
        dec     bx
        jnz     .sw
.sc:    movzx   si, dl                  ; row c / A[c][c]
        imul    si, si, 3
        movzx   bx, dl
        mov     al, [qt_ga+si+bx]
        call    gf_inv
        mov     ah, al
        mov     bx, 3
.sr:    mov     al, [qt_ga+si]
        call    gf_mul
        mov     [qt_ga+si], al
        mov     al, [qt_gm+si]
        call    gf_mul
        mov     [qt_gm+si], al
        inc     si
        dec     bx
        jnz     .sr
        xor     dh, dh                  ; the other rows: row r - A[r][c] * row c
.r:     cmp     dh, cl
        jae     .cn
        cmp     dh, dl
        je      .rn
        movzx   di, dh
        imul    di, di, 3
        movzx   bx, dl
        mov     ah, [qt_ga+di+bx]       ; the factor
        or      ah, ah
        jz      .rn
        movzx   si, dl
        imul    si, si, 3
        mov     bx, 3
.rr:    mov     al, [qt_ga+si]
        call    gf_mul
        xor     [qt_ga+di], al
        mov     al, [qt_gm+si]
        call    gf_mul
        xor     [qt_gm+di], al
        inc     si
        inc     di
        dec     bx
        jnz     .rr
.rn:    inc     dh
        jmp     .r
.cn:    inc     dl
        jmp     .c
.ok:    clc
        jmp     .o
.bad:   stc
.o:     pop     di
        pop     si
        pop     dx
        pop     bx
        pop     ax
        ret

; the column DI with the syndromes qt_syn: the erasures' errors (M S)
; into the buffer, and the syndromes left over checked.  CF if they do not
; agree.
qt_era_col:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        movzx   cx, byte [qt_nera]
        xor     si, si                  ; erasure j
.e:     xor     dl, dl
        mov     bx, si
        imul    bx, bx, 3               ; row j of M
        xor     dh, dh                  ; m
.m:     cmp     dh, cl
        jae     .put
        mov     al, [qt_gm+bx]
        push    bx
        movzx   bx, dh
        mov     ah, [qt_syn+bx]
        pop     bx
        call    gf_mul
        xor     dl, al
        inc     bx
        inc     dh
        jmp     .m
.put:   mov     [qt_err+si], dl
        movzx   ax, byte [qt_era+si]    ; into its byte
        shl     ax, 10
        add     ax, di
        add     ax, [qt_buf]
        mov     bx, ax
        xor     [bx], dl
        inc     si
        cmp     si, cx
        jb      .e
        mov     dh, cl                  ; the rows left: sum of R_r(X_j) e_j
.k:     cmp     dh, 3
        jae     .ok
        xor     dl, dl
        xor     si, si
.kj:    cmp     si, cx
        jae     .kc
        mov     al, [qt_err+si]
        or      al, al
        jz      .kn
        movzx   bx, byte [qt_elx+si]    ; e X or e / X: log e +- log X
        cmp     dh, 2
        jne     .k1
        neg     bx
        add     bx, 255
.k1:    push    si
        movzx   si, al
        movzx   si, byte [gf_log+si]
        add     bx, si
        pop     si
        xor     dl, [gf_exp+bx]
.kn:    inc     si
        jmp     .kj
.kc:    movzx   bx, dh
        cmp     dl, [qt_syn+bx]
        jne     .bad
        inc     dh
        jmp     .k
.ok:    clc
        jmp     .o
.bad:   stc
.o:     pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; the three parity sectors of the segment in the buffer: [qt_bcap] bytes
; of data, the parity after them
qt_encode:
        pushad
        mov     bp, [qt_bcap]
        shr     bp, 10                  ; data sectors
        xor     di, di                  ; column
.col:   mov     si, [qt_buf]
        add     si, di
        mov     cx, bp
        xor     dx, dx                  ; DL = r0, DH = r1
        xor     ah, ah                  ; AH = r2
.m:     mov     al, [si]                ; c = m ^ r0
        xor     al, dl
        movzx   bx, al
        mov     bl, [gf_c0+bx]          ; c * C0h
        mov     dl, dh                  ; r0 = r1 ^ t
        xor     dl, bl
        mov     dh, ah                  ; r1 = r2 ^ t
        xor     dh, bl
        mov     ah, al                  ; r2 = c
        add     si, 1024
        loop    .m
        mov     [si], dl                ; highest degree first
        mov     [si+1024], dh
        mov     [si+2048], ah
        inc     di
        cmp     di, 1024
        jb      .col
        popad
        ret
