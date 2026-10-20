# Solves ax^2 + bx + c = 0. Reads a, b, c as doubles from stdin,
# one per line, prints the roots. `a == 0` makes the equation linear.

# Every message has its byte count in the u64 right before the text,
# so `print` gets the length with one negative `ld` offset.

section .text

# Print the message at x1.
:print
    pushqr x4
    ld     x4, x1, -1      # length one slot below the text
    addi   x3, x1, 0       # buffer
    addi   x1, x0, 1       # write
    addi   x2, x0, 1       # stdout
    ecall
    popqr  x4
    ret

# Print the message at x1, then f0 as a number.
:print_root
    call :print
    outf f0
    ret

# Exit successfully.
:exit
    addi x1, x0, 0        # WST_VM_SYS_EXIT
    addi x2, x0, 0        # code
    ecall

:_start
    addi x1, x0, :msg_intro
    call :print
    inf  f0                # a
    inf  f1                # b
    inf  f2                # c

    fld  f7, x0, :fp_zero
    feq  x1, f0, f7
    bne  x1, x0, :linear

    fmul f3, f1, f1        # b*b
    fadd f4, f0, f0        # 2a
    fadd f4, f4, f4        # 4a
    fmul f4, f4, f2        # 4ac
    fsub f3, f3, f4        # D
    flt  x1, f3, f7        # D < 0 ?
    bne  x1, x0, :no_roots
    feq  x1, f3, f7        # D == 0 ?
    bne  x1, x0, :one_root

    # Two roots
    addi  x1, x0, :msg_two
    call  :print
    fsub  f6, f7, f1       # -b
    fadd  f5, f0, f0       # 2a
    fdiv  f6, f6, f5       # center
    fsqrt f4, f3
    fdiv  f4, f4, f5       # spread
    fadd  f0, f6, f4
    addi  x1, x0, :msg_x1
    call  :print_root
    fsub  f0, f6, f4
    addi  x1, x0, :msg_x2
    call  :print_root
    jal  x0, :exit

:one_root
    addi  x1, x0, :msg_one
    call  :print
    fadd  f5, f0, f0       # 2a
    fsub  f0, f7, f1       # -b
    fdiv  f0, f0, f5
    addi  x1, x0, :msg_x
    call  :print_root
    jal  x0, :exit

:no_roots
    addi x1, x0, :msg_none
    call :print
    jal  x0, :exit

:linear
    feq  x1, f1, f7        # b == 0 ?
    bne  x1, x0, :constant

    addi  x1, x0, :msg_linear
    call  :print
    fsub  f0, f7, f2       # -c
    fdiv  f0, f0, f1       # / b
    addi  x1, x0, :msg_x
    call  :print_root
    jal  x0, :exit

:constant
    feq  x1, f2, f7        # c == 0 ?
    bne  x1, x0, :any_x
    addi x1, x0, :msg_no_sol
    call :print
    jal  x0, :exit

:any_x
    addi x1, x0, :msg_any_x
    call :print
    jal  x0, :exit

section .data
    fp_zero:        f64 0.0

    msg_intro_len:  u64 34
    msg_intro:      str "ax^2 + bx + c, a b c one per line\n"

    msg_two_len:    u64 9
    msg_two:        str "2 roots:\n"

    msg_one_len:    u64 8
    msg_one:        str "1 root:\n"

    msg_none_len:   u64 14
    msg_none:       str "no real roots\n"

    msg_x1_len:     u64 5
    msg_x1:         str "x1 = "

    msg_x2_len:     u64 5
    msg_x2:         str "x2 = "

    msg_x_len:      u64 4
    msg_x:          str "x = "

    msg_linear_len: u64 17
    msg_linear:     str "linear equation:\n"

    msg_no_sol_len: u64 12
    msg_no_sol:     str "no solution\n"

    msg_any_x_len:  u64 16
    msg_any_x:      str "all x are roots\n"
