
# Example asm program, exercises every opcode.
# Reads one integer and one double from stdin.

# About ABI conventions:
# x0 is zero, x1-x3 take params with the return value in x1,
# x4-x5 are callee-saved, x6 is the link register, x7 is sp.
# x1-x3 and x6 are caller-saved, f0-f7 are caller-saved scratch
# with f0 doubling as float return.
# `call` pushes the return address, `ret` pops it.
# The stack grows down in 8 byte slots, direct access from x7
# with offsets does not have to be aligned.


section .text
:_test_out
    fld  f0, x0, :fp64_c  # f0 = fp64_c
    outf f0
    ret

:_test_write_stdout
    pushqr x4             # x4 callee-saved
    addi   x1, x0, 1      # number = write
    addi   x2, x0, 1      # stream = stdout
    addi   x3, x0, :hello_world
    ld     x4, x0, :hello_world_len
    ecall
    popqr  x4
    ret

:_test_int_arith_bitwise
    addi x1, x0, 2        # x1 = 2
    addi x2, x0, 3        # x2 = 3
    add  x3, x1, x2       # x3 = x1 + x2
    addi x1, x0, 4        # x1 = 4
    mul  x3, x3, x1       # x3 = x3 * x1
    outi x3               # should be (2 + 3) * 4 = 20

    add  x1, x0, x3       # x1 = x3
    addi x2, x0, 6        # x2 = 6
    sub  x3, x1, x2       # x3 = x1 - x2
    outi x3

    div  x3, x1, x2       # x3 = x1 / x2
    outi x3

    rem  x3, x1, x2       # x3 = x1 % x2
    outi x3

    and  x3, x1, x2       # x3 = x1 & x2
    outi x3

    or   x3, x1, x2       # x3 = x1 | x2
    outi x3

    xor  x3, x1, x2       # x3 = x1 ^ x2
    outi x3

    sll  x3, x1, x2       # x3 = x1 << x2
    outi x3

    srl  x3, x1, x2       # x3 = x1 >> x2
    outi x3

    addi x1, x0, -8       # x1 = -8
    sra  x3, x1, x2       # x3 = x1 >> x2 (signed)
    outi x3

    lui  x3, 0x1000       # x3 = 0x1000
    outi x3

    addi x1, x0, 20       # x1 = 20 
    addi x2, x0, 0        # x2 = 0
    div  x3, x1, x2       # -1,    #DE
    outi x3

    rem  x3, x1, x2       # x3 = x1 % x2 = 20, remainder by zero
    outi x3

    addi x2, x0, 6        # x2 = 6
    divu x3, x1, x2       # x3 = x1 / x2
    outi x3

    remu x3, x1, x2       # x3 = x1 % x2
    outi x3

    divu x3, x1, x0       # -1, divide by zero
    outi x3

    remu x3, x1, x0       # 20, remainder by zero
    outi x3
    ret

:_test_int_imm
    addi x1, x0, 20       # x1 = 20
    andi x3, x1, 6        # x3 = x1 & 6
    outi x3

    ori  x3, x1, 6        # x3 = x1 | 6
    outi x3

    xori x3, x1, 6        # x3 = x1 ^ 6
    outi x3

    slli x3, x1, 6        # x3 = x1 << 6
    outi x3

    srli x3, x1, 6        # x3 = x1 >> 6
    outi x3

    addi x1, x0, -8       # x1 = -8
    srai x3, x1, 6        # x3 = x1 >> 6 signed
    outi x3

    slti x3, x1, 6        # x3 = x1 < 6 signed
    outi x3

    sltiu x3, x1, 6       # x3 = x1 < 6 unsigned
    outi  x3
    ret

:_test_branches
    addi x1, x0, 10       # x1 = 10
    addi x2, x0, 20       # x2 = 20
    blt  x1, x2, :t1      # x1 < x2 ? goto t1 - taken
    addi x3, x0, 0        # x3 = 0
    jal  x0, :t1x         # goto t1x
:t1
    addi x3, x0, 1        # x3 = 1
:t1x
    outi x3               # should be 1
    bge  x1, x2, :t2      # x1 >= x2 ? goto t2 - not taken
    addi x3, x0, 2        # x3 = 2
    jal  x0, :t2x         # goto t2x
:t2
    addi x3, x0, 4        # x3 = 4
:t2x
    outi x3               # should be 2
    addi x1, x0, 7        # x1 = 7
    addi x2, x0, 7        # x2 = 7
    beq  x1, x2, :t3      # x1 == x2 ? goto t3 - taken
    addi x3, x0, 5        # x3 = 5
    jal  x0, :t3x         # goto t3x
:t3
    addi x3, x0, 6        # x3 = 6
:t3x
    outi x3               # should be 6
    bne  x1, x2, :t4      # x1 != x2 ? goto t4 - not taken
    addi x3, x0, 7        # x3 = 7
    jal  x0, :t4x         # goto t4x
:t4
    addi x3, x0, 8        # x3 = 8 
:t4x
    outi x3               # should be 7
    jal  x3, :j1          # x3 = pc, goto j1
    addi x3, x0, 0        # x3 = 0, dead code
:j1
    outi x3               # should be old pc
    jal  x6, :jsub        # link in x6, manual call
    outi x3               # 5, set by subroutine
    jal  x0, :jdone
:jsub
    addi x3, x0, 5
    jalr x0, x6, 0        # manual return through link
:jdone
    ret

:_test_auipc
    jal   x6, :lnx
:lnx
    auipc x3, 0
    sub   x1, x3, x6
    outi  x1              # 0, auipc read pc
    ret

:_test_mem
    addi   x1, x0, -42    # x1 = -42
    st     x0, x1, :m1    # M[m1] = -42
    ld     x2, x0, :m1    # x2 = M[m1]
    outi   x2

    addi   x1, x0, -7     # x1 = -7
    sw     x0, x1, :m2
    lw     x2, x0, :m2
    outi   x2             # -7, sign-extended

    ld     x2, x0, :m2
    outi   x2             # 4294967289, high bytes untouched

    fld    f0, x0, :fp64_a # 2.5
    fst    x0, f0, :m3    # M[m3] = 2.5 bits
    fld    f1, x0, :m3
    outf   f1             # 2.5

    addi   x1, x0, 123
    pushqr x1
    addi   x1, x0, 0
    popqr  x2
    outi   x2             # 123

    pushqv f0
    fld    f0, x0, :fp64_b # clobber f0 with 4.0
    popqv  f1
    outf   f1             # 2.5
    ret

:_test_fp_arith
    fld   f0, x0, :fp64_a # f0 = fp64_a = 2.5
    fld   f1, x0, :fp64_b # f1 = fp64_b = 4.0
    fadd  f2, f0, f1      # f2 = f0 + f1 = 2.5 + 4.0 = 6.5
    outf  f2

    fsub  f2, f1, f0      # f2 = f1 - f0 = 1.5
    outf  f2

    fmul  f2, f0, f1      # f2 = f0 * f1 = 2.5 * 4.0 = 10.0
    outf  f2

    fdiv  f2, f1, f0      # f2 = f1 / f0 = 1.6
    outf  f2

    fsqrt f2, f1          # f2 = sqrt(f1) = 2
    outf  f2

    feq   x1, f0, f0      # x1 = f0 == f0
    outi  x1              # 1

    feq   x1, f0, f1      # x1 = f0 == f1
    outi  x1              # 0

    flt   x1, f0, f1      # x1 = f0 < f1
    outi  x1              # 1

    fle   x1, f1, f0      # x1 = f1 <= f0
    outi  x1              # 0
    ret

:_test_callee_saved
    pushqr x4
    pushqr x5
    addi   x4, x0, 111
    addi   x5, x0, 222
    add    x1, x4, x5     # 333 in x1
    popqr  x5
    popqr  x4
    ret

:_test_in
    ini  x1
    outi x1
    inf  f0
    outf f0
    ret

:_start
    call :_test_int_arith_bitwise
    call :_test_fp_arith
    call :_test_out
    call :_test_write_stdout
    call :_test_int_imm
    call :_test_branches
    call :_test_auipc
    call :_test_mem
    addi x4, x0, 100
    addi x5, x0, 200
    call :_test_callee_saved
    outi x1               # 333
    outi x4               # 100, preserved
    outi x5               # 200, preserved
    call :_test_in
    hlt  0

section .data
    m1:     u64 0
    m2:     u64 0
    m3:     f64 0.0

    fp64_a: f64 2.5
    fp64_b: f64 4.0
    fp64_c: f64 1488.0

    hello_world:     str "Hello, world!\n\0"
    hello_world_len: u64 14
