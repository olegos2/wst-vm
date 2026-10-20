# Toy RISC-V ISA

Fixed 4 byte instructions, RISC-V inspired but simplified. No flags or
SIMD. The core is 64 bit. W operations return 32 bit results and `.s`
operations use single precision.

Machine memory layout and boot state: [memory-map](memory-map.md).

## Registers

Eight integer registers `x0` to `x7`. `x0` reads as zero and drops writes.

Eight floating registers `f0` to `f7`, holding raw 64 bit patterns.
`fld`/`fst` move doubles. `fadd`, `fsub`, `fmul`, `fdiv` and `fsqrt`
operate on those bits. `flw` loads a single and NaN-boxes it, `fsw`
stores the low 32 bits. `.s` operations require boxed singles and box
their results. An unboxed value reads as canonical NaN. Use `flw` for
`f32`, never `fld`. `outf` prints a boxed single, otherwise the double.

`pc` is a 4-aligned byte offset.

| Register | Hardware role | Owner convention | ABI convention |
| -------- | ------------- | ---------------- | -------------- |
| `x0`     | zero register | —                | ignores writes, reads as `0` |
| `x1`     | —             | caller           | argument or return value |
| `x2`     | —             | caller           | argument       |
| `x3`     | —             | caller           | argument       |
| `x4`     | —             | callee           | —              |
| `x5`     | —             | callee           | —              |
| `x6`     | —             | caller           | return address |
| `x7`     | —             | callee           | `sp`, stack pointer |

`x7` plus `ld`/`st`/`addi` is the stack convention. `pushqr`/`pushqv`/
`popqr`/`popqv` and `call`/`ret` use the same value stack.

## Instruction word

Each instruction is a 4 byte little-endian word. Low 7 bits hold the
opcode. Register fields are 3 bits.

| Format | Bit fields |
| ------ |:---------- |
| R      | `opcode[6:0]` `rd[9:7]` `rs1[12:10]` `rs2[15:13]` |
| R4     | `opcode[6:0]` `rd[9:7]` `rs1[12:10]` `rs2[15:13]` `rs3[18:16]` |
| I      | `opcode[6:0]` `rd[9:7]` `rs1[12:10]` `imm[31:13]`, signed 19 bits |
| S      | `opcode[6:0]` `rs1[9:7]` `rs2[12:10]` `imm[31:13]`, signed 19 bits |
| B      | `opcode[6:0]` `rs1[9:7]` `rs2[12:10]` `imm[31:13]`, same shape as S, imm counts from current address |
| U      | `opcode[6:0]` `rd[9:7]` `imm[31:12]`, top 20 bits |
| J      | `opcode[6:0]` `rd[9:7]` `imm[31:10]`, signed 22 bits from current address |

| Format | Used for |
| ------ |:-------- |
| R      | Register to register work with up to 3 registers |
| R4     | Fused multiply-add with 4 registers |
| I      | Loads, small constants and single register ops |
| S      | Stores with base from register and signed 19 bit offset |
| B      | Two register comparison and jump relative to current `pc` with signed 19 bit offset |
| U      | Building big constants |
| J      | Far jumps from current `pc` |

`call` reuses `J`, `ret` reuses `R` with no operands.

## Opcodes

`R[x]`/`F[x]` are integer/float registers, `M[a:n]` is `n` bytes at
address `a`, `S64`/`S32` sign-extend, `PC` is the current address and
`STACK` is the value stack. Integer arithmetic wraps modulo 2^64. In
`.s` rows, `f32(...)` reads a boxed single and the result is boxed.

| Opcode | Mnemonic   | Format | Operands               | Semantics                                         | Stands for |
| ------:|:---------- | ------ |:---------------------- | ------------------------------------------------- | ---------- |
| `0x01` | `outi`     | I      | `rs1`                  | `print(S64(R[rs1]))`                              | output integer |
| `0x02` | `outf`     | I      | `rs1`                  | `print(F[rs1])`                                   | output float |
| `0x03` | `ld`       | I      | `rd`  `rs1` `imm`      | `R[rd] := M[R[rs1] + 8*imm:8]`                    | load doubleword |
| `0x04` | `lw`       | I      | `rd`  `rs1` `imm`      | `R[rd] := S64(M[R[rs1] + 4*imm:4])`               | load word |
| `0x05` | `st`       | S      | `rs1` `rs2` `imm`      | `M[R[rs1] + 8*imm:8] := R[rs2]`                   | store doubleword |
| `0x06` | `sw`       | S      | `rs1` `rs2` `imm`      | `M[R[rs1] + 4*imm:4] := R[rs2][31:0]`             | store word |
| `0x07` | `fld`      | I      | `rd`  `rs1` `imm`      | `F[rd] := M[R[rs1] + 8*imm:8]`                    | fp load doubleword |
| `0x08` | `fst`      | S      | `rs1` `rs2` `imm`      | `M[R[rs1] + 8*imm:8] := F[rs2]`                   | fp store doubleword |
| `0x09` | `pushqr`   | I      | `rs1`                  | `STACK.push(R[rs1])`                              | push quadword from GPR |
| `0x0A` | `pushqv`   | I      | `rs1`                  | `STACK.push(F[rs1])`                              | push quadword from fp reg |
| `0x0B` | `popqr`    | I      | `rd`                   | `R[rd] := STACK.pop()`                            | pop quadword to GPR |
| `0x0C` | `popqv`    | I      | `rd`                   | `F[rd] := STACK.pop()`                            | pop quadword to fp reg |
| `0x0D` | `jal`      | J      | `rd`  `imm`            | `R[rd] := PC + 4; PC := PC+imm`                   | jump and link |
| `0x0E` | `beq`      | B      | `rs1` `rs2` `imm`      | `if S64(R[rs1]) == S64(R[rs2]): PC := PC+imm`     | branch if equal |
| `0x0F` | `bne`      | B      | `rs1` `rs2` `imm`      | `if S64(R[rs1]) != S64(R[rs2]): PC := PC+imm`     | branch if not equal |
| `0x10` | `blt`      | B      | `rs1` `rs2` `imm`      | `if S64(R[rs1]) < S64(R[rs2]): PC := PC+imm`      | branch if less than |
| `0x11` | `bge`      | B      | `rs1` `rs2` `imm`      | `if S64(R[rs1]) >= S64(R[rs2]): PC := PC+imm`     | branch if greater or equal |
| `0x12` | `add`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := R[rs1] + R[rs2]`                        | add GPRs |
| `0x13` | `sub`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := R[rs1] - R[rs2]`                        | subtract GPRs |
| `0x14` | `mul`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := R[rs1] * R[rs2]`                        | multiply GPRs |
| `0x15` | `div`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := sdiv(R[rs1], R[rs2])`                   | divide GPRs |
| `0x16` | `rem`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := srem(R[rs1], R[rs2])`                   | remainder GPRs |
| `0x17` | `and`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := R[rs1] & R[rs2]`                        | bitwise AND |
| `0x18` | `or`       | R      | `rd`  `rs1` `rs2`      | `R[rd] := R[rs1] \| R[rs2]`                       | bitwise OR |
| `0x19` | `xor`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := R[rs1] ^ R[rs2]`                        | bitwise XOR |
| `0x1A` | `sll`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := R[rs1] << (R[rs2] & 63)`                | shift left logical |
| `0x1B` | `srl`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := R[rs1] >> (R[rs2] & 63)`                | shift right logical |
| `0x1C` | `sra`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(R[rs1]) >> (R[rs2] & 63)`           | shift right arithmetic |
| `0x1D` | `addi`     | I      | `rd`  `rs1` `imm`      | `R[rd] := R[rs1] + S64(imm)`                      | add immediate |
| `0x1E` | `lui`      | U      | `rd`  `imm`            | `R[rd] := imm`                                    | load upper immediate |
| `0x1F` | `fadd`     | R      | `rd`  `rs1` `rs2`      | `F[rd] := F[rs1] + F[rs2]`                        | fp add |
| `0x20` | `fsub`     | R      | `rd`  `rs1` `rs2`      | `F[rd] := F[rs1] - F[rs2]`                        | fp subtract |
| `0x21` | `fmul`     | R      | `rd`  `rs1` `rs2`      | `F[rd] := F[rs1] * F[rs2]`                        | fp multiply |
| `0x22` | `fdiv`     | R      | `rd`  `rs1` `rs2`      | `F[rd] := F[rs1] / F[rs2]`                        | fp divide |
| `0x23` | `fsqrt`    | R      | `rd`  `rs1`            | `F[rd] := sqrt(F[rs1])`                           | fp square root |
| `0x25` | `feq`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := (F[rs1] == F[rs2]) ? 1 : 0`             | fp compare equal |
| `0x26` | `flt`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := (F[rs1] < F[rs2])? 1 : 0`               | fp compare less than |
| `0x27` | `fle`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := (F[rs1] <= F[rs2])? 1 : 0`              | fp compare less or equal |
| `0x28` | `ecall`    | R      |                        | `host(R[x1])`                                     | environment call, see table below |
| `0x29` | `call`     | J      | `imm`                  | `STACK.push(PC+4); PC := PC+imm`                  | call routine |
| `0x2A` | `ret`      | R      |                        | `PC := STACK.pop()`                               | return from routine |
| `0x2B` | `ini`      | I      | `rd`                   | `R[rd] := parse_int(stdin_line)`                  | input integer |
| `0x2C` | `inf`      | I      | `rd`                   | `F[rd] := parse_double(stdin_line)`               | input float |
| `0x2D` | `jalr`     | I      | `rd`  `rs1` `imm`      | `R[rd] := PC + 4; PC := (R[rs1] + S64(imm)) & ~1` | jump and link register |
| `0x2E` | `divu`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := udiv(R[rs1], R[rs2])`                   | unsigned divide |
| `0x2F` | `remu`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := urem(R[rs1], R[rs2])`                   | unsigned remainder |
| `0x30` | `andi`     | I      | `rd`  `rs1` `imm`      | `R[rd] := R[rs1] & S64(imm)`                      | bitwise AND immediate |
| `0x31` | `ori`      | I      | `rd`  `rs1` `imm`      | `R[rd] := R[rs1] \| S64(imm)`                     | bitwise OR immediate |
| `0x32` | `xori`     | I      | `rd`  `rs1` `imm`      | `R[rd] := R[rs1] ^ S64(imm)`                      | bitwise XOR immediate |
| `0x33` | `slli`     | I      | `rd`  `rs1` `imm`      | `R[rd] := R[rs1] << (S64(imm) & 63)`              | shift left logical immediate |
| `0x34` | `srli`     | I      | `rd`  `rs1` `imm`      | `R[rd] := R[rs1] >> (S64(imm) & 63)`              | shift right logical immediate |
| `0x35` | `srai`     | I      | `rd`  `rs1` `imm`      | `R[rd] := S64(R[rs1])>>(S64(imm) & 63)`           | shift right arithmetic immediate |
| `0x36` | `slti`     | I      | `rd`  `rs1` `imm`      | `R[rd] := (S64(R[rs1]) < S64(imm)) ? 1 : 0`       | set less than immediate, signed |
| `0x37` | `sltiu`    | I      | `rd`  `rs1` `imm`      | `R[rd] := (R[rs1] < U64(S64(imm))) ? 1 : 0`       | set less than immediate, unsigned |
| `0x38` | `auipc`    | U      | `rd`  `imm`            | `R[rd] := PC + imm`                               | add upper immediate to pc |
| `0x39` | `lwu`      | I      | `rd`  `rs1` `imm`      | `R[rd] := M[R[rs1] + 4*imm:4]`                    | load word unsigned |
| `0x3A` | `slt`      | R      | `rd`  `rs1` `rs2`      | `R[rd] := (S64(R[rs1]) < S64(R[rs2])) ? 1 : 0`    | set less than |
| `0x3B` | `sltu`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := (R[rs1] < R[rs2]) ? 1 : 0`              | set less than unsigned |
| `0x3C` | `bltu`     | B      | `rs1` `rs2` `imm`      | `if R[rs1] < R[rs2]: PC := PC+imm`                | branch if less than unsigned |
| `0x3D` | `bgeu`     | B      | `rs1` `rs2` `imm`      | `if R[rs1] >= R[rs2]: PC := PC+imm`               | branch if greater or equal unsigned |
| `0x3E` | `addw`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(S32(R[rs1] + R[rs2]))`              | add word |
| `0x3F` | `subw`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(S32(R[rs1] - R[rs2]))`              | subtract word |
| `0x40` | `mulw`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(S32(R[rs1] * R[rs2]))`              | multiply word |
| `0x41` | `divw`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(S32(R[rs1]) / S32(R[rs2]))`         | divide word |
| `0x42` | `divuw`    | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(U32(R[rs1]) / U32(R[rs2]))`         | divide word unsigned |
| `0x43` | `remw`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(S32(R[rs1]) % S32(R[rs2]))`         | remainder word |
| `0x44` | `remuw`    | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(U32(R[rs1]) % U32(R[rs2]))`         | remainder word unsigned |
| `0x45` | `sllw`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(S32(R[rs1] << (R[rs2] & 31)))`      | shift left word |
| `0x46` | `srlw`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(S32(U32(R[rs1]) >> (R[rs2] & 31)))` | shift right logical word |
| `0x47` | `sraw`     | R      | `rd`  `rs1` `rs2`      | `R[rd] := S64(S32(R[rs1]) >> (R[rs2] & 31))`      | shift right arithmetic word |
| `0x48` | `addiw`    | I      | `rd`  `rs1` `imm`      | `R[rd] := S64(S32(R[rs1] + imm))`                 | add immediate word |
| `0x49` | `slliw`    | I      | `rd`  `rs1` `imm`      | `R[rd] := S64(S32(R[rs1] << (imm & 31)))`         | shift left immediate word |
| `0x4A` | `srliw`    | I      | `rd`  `rs1` `imm`      | `R[rd] := S64(S32(U32(R[rs1]) >> (imm & 31)))`    | shift right logical immediate word |
| `0x4B` | `sraiw`    | I      | `rd`  `rs1` `imm`      | `R[rd] := S64(S32(R[rs1]) >> (imm & 31))`         | shift right arithmetic immediate word |
| `0x4C` | `flw`      | I      | `rd`  `rs1` `imm`      | `F[rd] := box(M[R[rs1] + 4*imm:4])`               | load word float |
| `0x4D` | `fsw`      | S      | `rs1` `rs2` `imm`      | `M[R[rs1] + 4*imm:4] := low32(F[rs2])`            | store word float |
| `0x4E` | `fcvt.s.w` | R      | `rd`  `rs1`            | `F[rd] := box(f32(S32(R[rs1])))`                  | convert word to fp32 |
| `0x4F` | `fcvt.w.s` | R      | `rd`  `rs1`            | `R[rd] := S64(f32_to_i32(unbox(F[rs1])))`         | convert fp32 to word |
| `0x50` | `fadd.s`   | R      | `rd`  `rs1` `rs2`      | `F[rd] := box(f32(F[rs1]) + f32(F[rs2]))`         | fp32 add |
| `0x51` | `fsub.s`   | R      | `rd`  `rs1` `rs2`      | `F[rd] := box(f32(F[rs1]) - f32(F[rs2]))`         | fp32 subtract |
| `0x52` | `fmul.s`   | R      | `rd`  `rs1` `rs2`      | `F[rd] := box(f32(F[rs1]) * f32(F[rs2]))`         | fp32 multiply |
| `0x53` | `fdiv.s`   | R      | `rd`  `rs1` `rs2`      | `F[rd] := box(f32(F[rs1]) / f32(F[rs2]))`         | fp32 divide |
| `0x54` | `fsqrt.s`  | R      | `rd`  `rs1`            | `F[rd] := box(sqrtf(f32(F[rs1])))`                | fp32 square root |
| `0x5B` | `feq.s`    | R      | `rd`  `rs1` `rs2`      | `R[rd] := (f32(F[rs1]) == f32(F[rs2])) ? 1 : 0`   | fp32 compare equal |
| `0x5C` | `flt.s`    | R      | `rd`  `rs1` `rs2`      | `R[rd] := (f32(F[rs1]) < f32(F[rs2])) ? 1 : 0`    | fp32 compare less |
| `0x5D` | `fle.s`    | R      | `rd`  `rs1` `rs2`      | `R[rd] := (f32(F[rs1]) <= f32(F[rs2])) ? 1 : 0`   | fp32 compare less or equal |
| `0x5E` | `fabs.s`   | R      | `rd`  `rs1`            | `F[rd] := box(fabsf(f32(F[rs1])))`                | fp32 absolute value |
| `0x5F` | `fneg.s`   | R      | `rd`  `rs1`            | `F[rd] := box(-f32(F[rs1]))`                      | fp32 negate |
| `0x60` | `fmax.s`   | R      | `rd`  `rs1` `rs2`      | `F[rd] := box(fmaxf(f32(F[rs1]), f32(F[rs2])))`   | fp32 maximum |
| `0x61` | `fmin.s`   | R      | `rd`  `rs1` `rs2`      | `F[rd] := box(fminf(f32(F[rs1]), f32(F[rs2])))`   | fp32 minimum |
| `0x62` | `fmv.s`    | R      | `rd`  `rs1`            | `F[rd] := box(f32(F[rs1]))`                       | copy floating register |
| `0x63` | `fmadd`    | R4     | `rd` `rs1` `rs2` `rs3` | `F[rd] := F[rs1] * F[rs2] + F[rs3]`               | fused multiply-add |
| `0x64` | `fmsub`    | R4     | `rd` `rs1` `rs2` `rs3` | `F[rd] := F[rs1] * F[rs2] - F[rs3]`               | fused multiply-subtract |
| `0x65` | `fnmadd`   | R4     | `rd` `rs1` `rs2` `rs3` | `F[rd] := -(F[rs1] * F[rs2]) - F[rs3]`            | fused negate multiply-add |
| `0x66` | `fnmsub`   | R4     | `rd` `rs1` `rs2` `rs3` | `F[rd] := -(F[rs1] * F[rs2]) + F[rs3]`            | fused negate multiply-subtract |
| `0x67` | `fmadd.s`  | R4     | `rd` `rs1` `rs2` `rs3` | `F[rd] := box(f32(F[rs1]) * f32(F[rs2]) + f32(F[rs3]))` | fp32 fused multiply-add |
| `0x68` | `fmsub.s`  | R4     | `rd` `rs1` `rs2` `rs3` | `F[rd] := box(f32(F[rs1]) * f32(F[rs2]) - f32(F[rs3]))` | fp32 fused multiply-subtract |
| `0x69` | `fnmadd.s` | R4     | `rd` `rs1` `rs2` `rs3` | `F[rd] := box(-(f32(F[rs1]) * f32(F[rs2])) - f32(F[rs3]))` | fp32 fused negate multiply-add |
| `0x6A` | `fnmsub.s` | R4     | `rd` `rs1` `rs2` `rs3` | `F[rd] := box(-(f32(F[rs1]) * f32(F[rs2])) + f32(F[rs3]))` | fp32 fused negate multiply-subtract |

Loads/stores use base plus offset in access-size units. Unaligned access
traps. Address arithmetic wraps modulo 2^64.

`ini`/`inf` read one stdin line (decimal or hex integer, or double).
Output is flushed before blocking. `EOF`, overlong lines and bad text are
fatal.

Divide by zero gives all bits set. Remainder by zero returns the
dividend. Most negative divided by minus one gives most negative with
zero remainder. Unsigned division only has the divide-by-zero rules.

`call`/`ret` use the value stack. `jal` links locally.
`jalr x0, x6, 0` returns through a register. Underflow and a misaligned
target are fatal.

## Word integer operations

W operations read the low 32 bits, produce a 32 bit result and
sign-extend it. Results wrap modulo 2^32 before extension. Shift counts
use the low 5 bits. `divw`/`remw` keep the 64 bit overflow rules.
`lwu` zero-extends. `sltu`, `bltu` and `bgeu` compare unsigned.

## Single precision floats

`fmax.s`/`fmin.s` follow IEEE NaN rules. `fcvt.w.s` truncates and
saturates. R4 forms round once. There are no transcendental
instructions: `sin`, `cos`, `atan2`, `log` and `pow` are software.
[mandelbulb.asm](../examples/vm/mandelbulb.asm) has small single
precision implementations built from arithmetic, `fsqrt.s` and `fmadd.s`.

## Narrow loads and stores

`ld`/`st` use 8 byte units. `lw`/`sw` use 4 byte units. `lw`
sign-extends, `lwu` zero-extends, and `sw` writes only the low 32 bits.
`fld`/`fst` move raw doubles. `flw`/`fsw` move boxed singles. Unaligned
access traps and the assembler rejects misaligned `:name` uses.

## Building constants

`addi` builds 19 immediate bits. `lui` takes a 32 bit value with low 12
bits zero. With `addi` it builds any 32 bit constant. Larger constants
need the data section or shifts. `auipc` reads `pc` for relative data.

## Host calls

`ecall` number is passed in `x1`, arguments are passed in `x2` to `x4`,
return value is passed in `x1`.

| Number | Name    | Arguments    | Description |
| ------:|:------- |:------------ | ----------- |
|    `0` | `exit`  | code         | End the program, host reads `code & 0xFF` from `x2` |
|    `1` | `write` | fd, buf, len | Write `len` bytes from `buf` to `fd`, return bytes written |
|    `2` | `flush` | —            | Wait for the host to present a frame, return 0 |

`write` allows stdout (`1`) and stderr (`2`). Other fds are fatal. The
range must be guest memory. Host errors are returned as-is.

`flush` returns 0 and makes `wst_vm_execute` report
`WST_VM_RUN_FLUSH`. The host presents a frame and resumes. The vm
example paces frames to the host refresh.

## Program files

The header mirrors ELF names (`WstProgHeader`/`WstProgSegment` in
`toys/prog.h`): magic `7f 57 53 54`, version `1`, segment array and
entry. The assembler emits text at address zero and data after it,
8-aligned. The loader validates the header, segments, address cap,
overlaps and entry before copying. The bss gap stays zero.

## Assembly syntax

The assembler (`toys/asm.h`, `asm` example) works in two passes over
the source text. Blank lines and `#` comments are skipped. There are
two sections, default at start is `section .text`:

```
section .text
:add_two
addi x1, x1, 2
ret

section .data
msg: str "hello, world!\n"
pi: f64 3.14159
half: f32 0.5
n: u64 0x100
w: u32 42
blob: bytes 7f 45 4c 46
```

- Code: `mnemonic operands...`, registers `x0`-`x7`/`f0`-`f7`,
  decimal or hex numbers, or `:label` on its own line. Labels point at
  the next instruction.
- Data: `name: <type> <value>`. Types are `u64`/`u32`, `f32`/`f64`,
  `str "..."` with `\n`, `\\`, `\"`, `\0`, `\t` escapes, and `bytes`
  with hex pairs. `u64`/`f64` align to 8, `u32`/`f32` to 4. Strings and
  bytes are exact with no NUL. Numbers are little-endian. Load `f32`
  with `flw`, not `fld`.
- Names are shared between code and data. Redefinition is an error.
- `:name` is a label or data address. The first pass records fixups.
  The second patches them once code length is known. `B`/`J`
  (`beq`, `jal`, `call`) are PC-relative, `I`/`S`/`U` (`ld`, `addi`,
  `st`, `lui`) are absolute. `lui` needs 4096 alignment. Unknown or
  out-of-range names are errors.

The disassembler (`asm --disasm`, `wst_vm_disasm`) prints code with
`:_start` and `:label_<addr>` at jump targets, and data as `bytes`
lines, so its output assembles back identically. It uses the same
`wst_vm_decode_word` and `wst_vm_instr_text` as the interpreter.
