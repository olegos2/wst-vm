
# Example asm program, exercises every opcode.
# Reads one integer and one double from stdin.
# Build with: asm prog.asm prog.bin

# About ABI conventions:
# x0 is zero, x1-x4 take params with the return value in x1,
# x5-x6 are callee-saved, x7 is the stack pointer.
# Floats are caller-saved scratch, f0 doubles as float return.
# `call` pushes the return address, `ret` pops it.
# The stack grows down in 8 byte slots, direct access from x7
# with offsets does not have to be aligned.


section .text
:_test_out
    fld f0, x0, :c     # f0 = c
    outf f0
    ret

:_test_write_stdout
    addi x1, x0, 1     # write
    addi x2, x0, 1     # stdout
    addi x3, x0, :hello_world
    addi x4, x0, 14    # len
    ecall
    ret

:_test_int_arith_bitwise
    addi x1, x0, 2     # x1 = 2
    addi x3, x0, 3     # x3 = 3
    add  x4, x1, x3    # x4 = x1 + x3
    addi x1, x0, 4     # x1 = 4
    mul  x3, x4, x1    # x3 = x4 * x1
    outi x3            # should be (2 + 3) * 4 = 20

    add  x1, x0, x3    # x1 = x3
    addi x2, x0, 6     # x2 = 6
    sub  x3, x1, x2    # x3 = x1 - x2
    outi x3

    div  x3, x1, x2    # x3 = x1 / x2
    outi x3

    mod  x3, x1, x2    # x3 = x1 % x2
    outi x3

    and  x3, x1, x2    # x3 = x1 & x2
    outi x3

    or   x3, x1, x2    # x3 = x1 | x2
    outi x3

    xor  x3, x1, x2    # x3 = x1 ^ x2
    outi x3

    sll  x3, x1, x2    # x3 = x1 << x2
    outi x3

    srl  x3, x1, x2    # x3 = x1 >> x2
    outi x3

    addi x1, x0, -8    # x1 = -8
    sra  x3, x1, x2    # x3 = x1 >> x2 (signed)
    outi x3

    lui  x3, 0x1000    # x3 = 0x1000
    outi x3

    addi x1, x0, 20    # x1 = 20 
    addi x2, x0, 0     # x2 = 0
    div  x3, x1, x2    # -1, #DE
    outi x3

    mod  x3, x1, x2    # x3 = x1 % x2 = 20, remainder by zero
    outi x3
    ret

:_test_branches
    addi x1, x0, 10    # x1 = 10
    addi x2, x0, 20    # x2 = 20
    blt x1, x2, :t1    # x1 < x2 ? goto t1 - taken
    addi x3, x0, 0     # x3 = 0
    jal x0, :t1x       # goto t1x
:t1
    addi x3, x0, 1     # x3 = 1
:t1x
    outi x3            # should be 1
    bge x1, x2, :t2    # x1 >= x2 ? goto t2 - not taken
    addi x3, x0, 2     # x3 = 2
    jal x0, :t2x       # goto t2x
:t2
    addi x3, x0, 4     # x3 = 4
:t2x
    outi x3            # should be 2
    addi x1, x0, 7     # x1 = 7
    addi x2, x0, 7     # x2 = 7
    beq x1, x2, :t3    # x1 == x2 ? goto t3 - taken
    addi x3, x0, 5     # x3 = 5
    jal x0, :t3x       # goto t3x
:t3
    addi x3, x0, 6     # x3 = 6
:t3x
    outi x3            # should be 6
    bne x1, x2, :t4    # x1 != x2 ? goto t4 - not taken
    addi x3, x0, 7     # x3 = 7
    jal x0, :t4x       # goto t4x
:t4
    addi x3, x0, 8     # x3 = 8 
:t4x
    outi x3            # should be 7
    jal x3, :j1        # x3 = pc, goto j1
    addi x3, x0, 0     # x3 = 0, dead code
:j1
    outi x3            # should be old pc
    ret

:_test_mem
    addi x1, x0, -42   # x1 = -42
    st x0, x1, :m1     # M[m1] = -42
    ld x2, x0, :m1     # x2 = M[m1]
    outi x2

    addi x1, x0, -7    # x1 = -7
    sw x0, x1, :m2
    lw x2, x0, :m2
    outi x2            # -7, sign-extended
    ld x2, x0, :m2
    outi x2            # 4294967289, high bytes untouched
    fld f0, x0, :a     # 2.5
    fst x0, f0, :m3    # M[m3] = 2.5 bits
    fld f1, x0, :m3
    outf f1            # 2.5
    addi x1, x0, 123
    pushqr x1
    addi x1, x0, 0
    popqr x2
    outi x2            # 123
    pushqv f0
    fld f0, x0, :b     # clobber f0 with 4.0
    popqv f1
    outf f1            # 2.5
    ret

:_test_fp_arith
    fld  f0, x0, :a    # f0 = a = 2.5
    fld  f1, x0, :b    # f1 = b = 4.0
    fadd f2, f0, f1    # f2 = f0 + f1 = 2.5 + 4.0 = 6.5
    outf f2

    fsub f2, f1, f0    # f2 = f1 - f0 = 1.5
    outf f2

    fmul f2, f0, f1    # f2 = f0 * f1 = 2.5 * 4.0 = 10.0
    outf f2

    fdiv f2, f1, f0    # f2 = f1 / f0 = 1.6
    outf f2

    fsqrt f2, f1       # f2 = sqrt(f1) = 2
    outf f2

    feq x1, f0, f0     # x1 = f0 == f0
    outi x1            # 1

    feq x1, f0, f1     # x1 = f0 == f1
    outi x1            # 0

    flt x1, f0, f1     # x1 = f0 < f1
    outi x1            # 1

    fle x1, f1, f0     # x1 = f1 <= f0
    outi x1            # 0

    ret

:_test_callee_saved
    pushqr x5
    pushqr x6
    addi x5, x0, 111
    addi x6, x0, 222
    add x1, x5, x6     # 333 in x1
    popqr x6
    popqr x5
    ret

:_test_in
    ini x1
    outi x1
    inf f0
    outf f0
    ret

:_start
    call :_test_int_arith_bitwise
    call :_test_fp_arith
    call :_test_out
    addi x2, x0, 1
    addi x3, x0, :hello_world
    addi x4, x0, 14
    call :_test_write_stdout
    call :_test_int_bitwise
    call :_test_branches
    call :_test_mem
    call :_test_fp_extra
    addi x5, x0, 100
    addi x6, x0, 200
    call :_test_callee_saved
    outi x1            # 333
    outi x5            # 100, preserved
    outi x6            # 200, preserved
    call :_test_in
    hlt  0

section .data
    a: f64 2.5
    b: f64 4.0
    c: f64 1488.0
    hello_world: str "Hello, world!\n\0"
    m1: u64 0
    m2: u64 0
    m3: f64 0.0
