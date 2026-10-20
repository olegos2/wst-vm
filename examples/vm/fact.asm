# Recursive factorial. Reads N and prints N! as the only result.
# Values above 20! do not fit in 64 bits.

section .text

# n! of x1, result in x1.
:fact
    slti x2, x1, 1         # n < 1 ?
    beq  x2, x0, :fact_rec
    addi x1, x0, 1         # 0! and negative input
    ret

:fact_rec
    pushqr x1              # save n
    addi   x1, x1, -1
    call   :fact
    popqr  x2              # n
    mul    x1, x1, x2
    ret

:_start
    ini  x1
    call :fact
    outi x1
    addi x1, x0, 0        # WST_VM_SYS_EXIT
    addi x2, x0, 0        # success
    ecall
