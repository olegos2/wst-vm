
# Prints 20 and 6.5, then ends. Integers come from `addi`.
# Doubles come from the data segment through `fld`.
# Build with: asm prog.asm prog.bin

section .text
:_start
    addi x1, x0, 2
    addi x2, x0, 3
    add  x3, x1, x2
    addi x4, x0, 4
    mul  x3, x3, x4
    outi x3
    fld  f0, x0, :a
    fld  f1, x0, :b
    fadd f2, f0, f1
    outf f2
    hlt  0

section .data
    a: f64 2.5
    b: f64 4.0
