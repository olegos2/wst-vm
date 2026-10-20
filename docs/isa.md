# Toy RISC-V ISA

Small virtual machine with fixed 4 byte instructions. It mimics layouts
and naming from RISC-V but simplified. No flags and no SIMD yet.

Machine memory layout and boot state are in
[memory-map](memory-map.md).

## Registers

Eight 64 bit integer registers named `x0` to `x7`. Register `x0` always
reads as zero and drops writes. This gives free idioms. Jump with no link
target is just a jump. Address zero plus offset reaches absolute memory.
Subtract into `x0` compares without keeping the result.

Eight double registers named `f0` to `f7`. Plain IEEE doubles, no extra
rules.

`pc` is a byte offset into the address space. It must stay aligned to 4.
Odd targets trap instead of running garbage.

| Register | Hardware role | Owner convention | ABI convention |
| -------- | ------------- | ---------------- | -------------- |
| `x0`     | zero register | —                | ignores writes, reads as `0` |
| `x1`     | —             | caller           | argument or return value |
| `x2`     | —             | caller           | argument       |
| `x3`     | —             | caller           | argument       |
| `x4`     | —             | callee           | —              |
| `x5`     | —             | callee           | —              |
| `x6`     | —             | caller           | return address |
| `x7`     | —             | callee           | `sp`, stack pointer  |

RISC-V has no dedicated stack ops either: `sp` plus `ld`/`st`/`addi`
is the convention there too. Our `pushqr`/`pushqv`/`popqr`/`popqv`
plus `call`/`ret` are conveniences over the same idea.

## Instruction word

Every instruction is 4 byte little-endian word. Low 7 bits hold the
opcode. The rest depends on the format. Register fields are 3 bits wide,
so values 0 to 7.

| Format | Bit fields |
| ------ |:---------- |
| R      | `opcode[6:0]` `rd[9:7]` `rs1[12:10]` `rs2[15:13]` |
| I      | `opcode[6:0]` `rd[9:7]` `rs1[12:10]` `imm[31:13]`, signed 19 bits |
| S      | `opcode[6:0]` `rs1[9:7]` `rs2[12:10]` `imm[31:13]`, signed 19 bits |
| B      | `opcode[6:0]` `rs1[9:7]` `rs2[12:10]` `imm[31:13]`, same shape as S, imm counts from current address |
| U      | `opcode[6:0]` `rd[9:7]` `imm[31:12]`, top 20 bits |
| J      | `opcode[6:0]` `rd[9:7]` `imm[31:10]`, signed 22 bits from current address |

| Format | Used for |
| ------ |:-------- |
| R      | Register to register work with up to 3 registers |
| I      | Loads, small constants and single register ops |
| S      | Stores with base from register and signed 19 bit offset |
| B      | Two register comparison and jump relative to current `pc` with signed 19 bit offset |
| U      | Building big constants |
| J      | Far jumps from current `pc` |

`call` reuses `J`, `ret` reuses `R` with no operands.

## Opcodes

How to read the semantics column:
`R[x]` is integer register `x`,
`F[f]` is float register `f`,
`M[a:n]` is `n` bytes at address `a`,
`S64`/`S32` are signed views or sign extension op,
`PC` is the current address,
`STACK` is the memory-backed value stack.
All integer arithmetic wraps modulo 2^64.

| Opcode | Mnemonic | Format | Operands          | Semantics                                         | Stands for |
| ------:|:-------- | ------ |:----------------- | ------------------------------------------------- | ---------- |
| `0x01` | `outi`   | I      | `rs1`             | `print(S64(R[rs1]))`                              | output integer |
| `0x02` | `outf`   | I      | `rs1`             | `print(F[rs1])`                                   | output float |
| `0x03` | `ld`     | I      | `rd`  `rs1` `imm` | `R[rd] := M[R[rs1] + 8*imm:8]`                    | load doubleword |
| `0x04` | `lw`     | I      | `rd`  `rs1` `imm` | `R[rd] := S64(M[R[rs1] + 4*imm:4])`               | load word |
| `0x05` | `st`     | S      | `rs1` `rs2` `imm` | `M[R[rs1] + 8*imm:8] := R[rs2]`                   | store doubleword |
| `0x06` | `sw`     | S      | `rs1` `rs2` `imm` | `M[R[rs1] + 4*imm:4] := R[rs2][31:0]`             | store word |
| `0x07` | `fld`    | I      | `rd`  `rs1` `imm` | `F[rd] := M[R[rs1] + 8*imm:8]`                    | float load doubleword |
| `0x08` | `fst`    | S      | `rs1` `rs2` `imm` | `M[R[rs1] + 8*imm:8] := F[rs2]`                   | float store doubleword |
| `0x09` | `pushqr` | I      | `rs1`             | `STACK.push(R[rs1])`                              | push quadword from GPR |
| `0x0A` | `pushqv` | I      | `rs1`             | `STACK.push(F[rs1])`                              | push quadword from fp reg |
| `0x0B` | `popqr`  | I      | `rd`              | `R[rd] := STACK.pop()`                            | pop quadword to GPR |
| `0x0C` | `popqv`  | I      | `rd`              | `F[rd] := STACK.pop()`                            | pop quadword to fp reg |
| `0x0D` | `jal`    | J      | `rd`  `imm`       | `R[rd] := PC + 4; PC := PC+imm`                   | jump and link |
| `0x0E` | `beq`    | B      | `rs1` `rs2` `imm` | `if S64(R[rs1]) == S64(R[rs2]): PC := PC+imm`     | branch if equal |
| `0x0F` | `bne`    | B      | `rs1` `rs2` `imm` | `if S64(R[rs1]) != S64(R[rs2]): PC := PC+imm`     | branch if not equal |
| `0x10` | `blt`    | B      | `rs1` `rs2` `imm` | `if S64(R[rs1]) < S64(R[rs2]): PC := PC+imm`      | branch if less than |
| `0x11` | `bge`    | B      | `rs1` `rs2` `imm` | `if S64(R[rs1]) >= S64(R[rs2]): PC := PC+imm`     | branch if greater or equal |
| `0x12` | `add`    | R      | `rd`  `rs1` `rs2` | `R[rd] := R[rs1] + R[rs2]`                        | add GPRs |
| `0x13` | `sub`    | R      | `rd`  `rs1` `rs2` | `R[rd] := R[rs1] - R[rs2]`                        | subtract GPRs |
| `0x14` | `mul`    | R      | `rd`  `rs1` `rs2` | `R[rd] := R[rs1] * R[rs2]`                        | multiply GPRs |
| `0x15` | `div`    | R      | `rd`  `rs1` `rs2` | `R[rd] := sdiv(R[rs1], R[rs2])`                   | divide GPRs |
| `0x16` | `rem`    | R      | `rd`  `rs1` `rs2` | `R[rd] := srem(R[rs1], R[rs2])`                   | remainder GPRs |
| `0x17` | `and`    | R      | `rd`  `rs1` `rs2` | `R[rd] := R[rs1] & R[rs2]`                        | bitwise AND |
| `0x18` | `or`     | R      | `rd`  `rs1` `rs2` | `R[rd] := R[rs1] \| R[rs2]`                       | bitwise OR |
| `0x19` | `xor`    | R      | `rd`  `rs1` `rs2` | `R[rd] := R[rs1] ^ R[rs2]`                        | bitwise XOR |
| `0x1A` | `sll`    | R      | `rd`  `rs1` `rs2` | `R[rd] := R[rs1] << (R[rs2] & 63)`                | shift left logical |
| `0x1B` | `srl`    | R      | `rd`  `rs1` `rs2` | `R[rd] := R[rs1] >> (R[rs2] & 63)`                | shift right logical |
| `0x1C` | `sra`    | R      | `rd`  `rs1` `rs2` | `R[rd] := S64(R[rs1]) >> (R[rs2] & 63)`           | shift right arithmetic |
| `0x1D` | `addi`   | I      | `rd`  `rs1` `imm` | `R[rd] := R[rs1] + S64(imm)`                      | add immediate |
| `0x1E` | `lui`    | U      | `rd`  `imm`       | `R[rd] := imm << 12`                              | load upper immediate |
| `0x1F` | `fadd`   | R      | `rd`  `rs1` `rs2` | `F[rd] := F[rs1] + F[rs2]`                        | float add |
| `0x20` | `fsub`   | R      | `rd`  `rs1` `rs2` | `F[rd] := F[rs1] - F[rs2]`                        | float subtract |
| `0x21` | `fmul`   | R      | `rd`  `rs1` `rs2` | `F[rd] := F[rs1] * F[rs2]`                        | float multiply |
| `0x22` | `fdiv`   | R      | `rd`  `rs1` `rs2` | `F[rd] := F[rs1] / F[rs2]`                        | float divide |
| `0x23` | `fsqrt`  | R      | `rd`  `rs1`       | `F[rd] := sqrt(F[rs1])`                           | float square root |
| `0x25` | `feq`    | R      | `rd`  `rs1` `rs2` | `R[rd] := (F[rs1] == F[rs2]) ? 1 : 0`             | float compare equal |
| `0x26` | `flt`    | R      | `rd`  `rs1` `rs2` | `R[rd] := (F[rs1] < F[rs2])? 1 : 0`               | float compare less than |
| `0x27` | `fle`    | R      | `rd`  `rs1` `rs2` | `R[rd] := (F[rs1] <= F[rs2])? 1 : 0`              | float compare less or equal |
| `0x28` | `ecall`  | R      |                   | `host(R[x1])`                                     | environment call, see table below |
| `0x29` | `call`   | J      | `imm`             | `STACK.push(PC+4); PC := PC+imm`                  | call routine |
| `0x2A` | `ret`    | R      |                   | `PC := STACK.pop()`                               | return from routine |
| `0x2B` | `ini`    | I      | `rd`              | `R[rd] := parse_int(stdin_line)`                  | input integer |
| `0x2C` | `inf`    | I      | `rd`              | `F[rd] := parse_double(stdin_line)`               | input float |
| `0x2D` | `jalr`   | I      | `rd`  `rs1` `imm` | `R[rd] := PC + 4; PC := (R[rs1] + S64(imm)) & ~1` | jump and link register |
| `0x2E` | `divu`   | R      | `rd`  `rs1` `rs2` | `R[rd] := udiv(R[rs1], R[rs2])`                   | unsigned divide |
| `0x2F` | `remu`   | R      | `rd`  `rs1` `rs2` | `R[rd] := urem(R[rs1], R[rs2])`                   | unsigned remainder |
| `0x30` | `andi`   | I      | `rd`  `rs1` `imm` | `R[rd] := R[rs1] & S64(imm)`                      | bitwise AND immediate |
| `0x31` | `ori`    | I      | `rd`  `rs1` `imm` | `R[rd] := R[rs1] \| S64(imm)`                     | bitwise OR immediate |
| `0x32` | `xori`   | I      | `rd`  `rs1` `imm` | `R[rd] := R[rs1] ^ S64(imm)`                      | bitwise XOR immediate |
| `0x33` | `slli`   | I      | `rd`  `rs1` `imm` | `R[rd] := R[rs1] << (S64(imm) & 63)`              | shift left logical immediate |
| `0x34` | `srli`   | I      | `rd`  `rs1` `imm` | `R[rd] := R[rs1] >> (S64(imm) & 63)`              | shift right logical immediate |
| `0x35` | `srai`   | I      | `rd`  `rs1` `imm` | `R[rd] := S64(R[rs1])>>(S64(imm) & 63)`           | shift right arithmetic immediate |
| `0x36` | `slti`   | I      | `rd`  `rs1` `imm` | `R[rd] := (S64(R[rs1]) < S64(imm)) ? 1 : 0`       | set less than immediate, signed |
| `0x37` | `sltiu`  | I      | `rd`  `rs1` `imm` | `R[rd] := (R[rs1] < U64(S64(imm))) ? 1 : 0`       | set less than immediate, unsigned |
| `0x38` | `auipc`  | U      | `rd`  `imm`       | `R[rd] := PC + (imm << 12)`                       | add upper immediate to pc |

Loads read from base register plus offset scaled to access size units,
stores write the same way. Unaligned addresses trap. Address arithmetic
wraps modulo 2^64, so negative offsets address below the base.

`ini`/`inf` read one stdin line each (decimal or hex integers for `ini`,
doubles for `inf`, surrounding whitespace is fine). Output is flushed
before blocking on input. `EOF`, overlong lines and unparsable text are
fatal, same as bad memory.

Integer divide by zero gives all bits set. Remainder by zero gives back
the dividend. Most negative divided by minus one gives most negative
with zero remainder. Unsigned division has no overflow case, only the
divide-by-zero rules carry over. This matches RISC-V and keeps behavior
defined for every input.

`call`/`ret` use the value stack, not a link register. `jal` stays for
local jumps with link, `jalr x0, x6, 0` returns through one. Underflow
on `ret` and misaligned targets are fatal, same as `popqr`/`popqv`.

## Narrow loads and stores

`ld`/`st` move whole 8 byte registers, the immediate counts in units
of 8. `lw`/`sw` move 4 bytes, the immediate counts in units of 4:

- `lw rd, rs1, imm` reads 4 bytes at `R[rs1]+4*imm` and sign-extends
  the 32 bit value into 64 bits.
- `sw rs1, rs2, imm` writes the low 4 bytes of `R[rs2]` at
  `R[rs1]+4*imm` and leaves the other 4 bytes of the slot alone.
  It never widens, so packing two 32 bit values side by side works.
- `fld`/`fst` always move 8 bytes raw between memory and float
  registers, no conversion. There are no narrow float ops.

Unaligned addresses trap, the assembler rejects misaligned `:name`
uses outright. Sign extension only happens on `lw`, never on stores.

## Building constants

`addi` can build up to 19 immediate bits.
`lui` combined with `addi` can build up to 32 immediate bits.
Building more than 32 immediate bits requires using data section or
shifts.

`auipc` can be used in position-independent code to read `pc` and
address data relatively.

## Host calls

`ecall` number is passed in `x1`, arguments are passed in `x2` to `x4`,
return value is passed in `x1`.

| Number | Name    | Arguments    | Description |
| ------:|:------- |:------------ | ----------- |
|    `0` | `exit`  | code         | End the program with `code & 0xFF` |
|    `1` | `write` | fd, buf, len | Write `len` bytes from `buf` to `fd`, return bytes written |
|    `2` | `flush` | —            | Wait for the host to present a frame, return 0 |

Write only allows standard output (`1`) and standard error (`2`).
Other file numbers are fatal. The address range must lie inside
guest memory. Host errors come back as they are.

`flush` is the frame boundary for the simple framebuffer. It returns
0 in `x1` and makes `wst_vm_execute` report `WST_VM_RUN_FLUSH`; the
host presents a frame, then resumes execution. The vm example paces
that presentation to the host refresh, so back-to-back flushes run
at most once per refresh. A host without a display may ignore it.

## Program files

Files start with a tiny header that mirrors ELF field names
(`WstProgHeader`/`WstProgSegment` in `toys/prog.h`). Magic bytes
are `7f 57 53 54`, version is `1`. The header points at an array of
segments and names the entry offset. The assembler links one text
segment at address zero and one data segment right after the code
(first 8-aligned address past it). Each segment header contains file
offset, target address, file size and memory size. The loader checks the whole
file before touching memory. Magic, version, header bounds, segment bounds,
address cap, overlapping segments and a sane aligned entry all get verified
first. Then segments copy over and the gap between file size and memory size
stays zero.

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
n: u64 0x100
w: u32 42
blob: bytes 7f 45 4c 46
```

- Code lines are `mnemonic operands...` with `x0`-`x7`/`f0`-`f7`
  registers and decimal or hex numbers, or a `:label` definition alone
  on a line. Labels address the start of the next instruction.
- Data entries are `name: <type> <value>` on one line. Types are
  `u64`/`u32` (numbers), `f64` (double), `str "..."` with `\n`,
  `\\`, `\"`, `\0`, `\t` escapes, and `bytes` with hex pairs like
  `01 02 ff` (spaces optional). `u64`/`f64` align to 8 bytes,
  `u32` to 4, strings and bytes pack exact bytes with no NUL added.
  Numbers store little-endian, `bytes` pairs are in memory order.
- Code and data share one namespace: redefining a name is an error.
- A `:name` operand refers to a label or a data entry. First pass
  emits words and records every `:name` as a fixup, second pass
  patches the machine code once code length (therefore, data addresses)
  is known. `B`/`J` immediates (`beq`, `jal`, `call`) are PC-relative
  (`target - ins_addr`), `I`/`S`/`U` immediates (`ld`, `addi`, `st`,
  `lui`) are absolute addresses. `lui` needs a 4096-aligned address.
  Unknown names and out-of-range immediates are errors.

The disassembler (`asm --disasm`, `wst_vm_disasm`) prints the whole
code segment as assembly text, with `:_start` at the entry and
`:label_<addr>` at branch and jump targets. It emits the data segment
as `bytes` lines, so its output assembles back to an identical
program file. It uses `wst_vm_decode_word` and `wst_vm_instr_text`,
the same decode and text the interpreter uses.

## Example program

See [prog.asm](examples/vm/prog.asm) for the full ISA and
[circle.asm](examples/vm/circle.asm) for the simple framebuffer.
The circle demo centers a 201x201 box, fills pixels with
`dx*dx + dy*dy <= 100*100` and flushes once per frame.
