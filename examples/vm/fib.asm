# Recursive fibonacci. Reads N and prints F(0) to F(N),
# one number per line.

section .text

# F(n) of x1, result in x1.
:fib
    slti x2, x1, 2         # n < 2 ?
    beq  x2, x0, :fib_rec
    ret

:fib_rec
    pushqr x1              # save n
    addi   x1, x1, -1      # x1 = n - 1
    call   :fib            # x1 = F(x1)
    pushqr x1              # save F(n-1)
    ld     x1, x7, 1       # pop n
    addi   x1, x1, -2      # x1 -= 2
    call   :fib            # x1 = F(x1)
    popqr  x2              # x2 = F(n - 1)
    add    x1, x1, x2      # x1 += x2
    popqr  x2              # drop n
    ret

:_start
    ini  x4                # N
    addi x5, x0, 0         # i
:loop
    blt  x4, x5, :done     # if (i >= N) break
    add  x1, x0, x5
    call :fib              # F(i)
    outi x1
    addi x5, x5, 1         # i++
    jal  x0, :loop
:done
    addi x1, x0, 0        # WST_VM_SYS_EXIT
    addi x2, x0, 0        # success
    ecall
