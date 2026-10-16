# Toy RISC-V ISA

Small virtual machine with fixed 4 byte instructions. It borrows layouts
and naming from RISC-V but stays tiny on purpose. Eight integer registers
and eight floating point registers. No flags and no SIMD yet.

## Registers

Eight 64 bit integer registers named `x0` to `x7`. Register `x0` always
reads as zero and drops writes. This gives free idioms. Jump with no link
target is just a jump. Address zero plus offset reaches absolute memory.
Subtract into `x0` compares without keeping the result.

Eight double registers named `f0` to `f7`. Plain IEEE doubles, no extra
rules. Hardware already turns bad input into `inf` and `nan`.

`pc` is a byte offset into the address space. It must stay aligned to 4.
Odd targets trap instead of running garbage.

`x0` is zero, `x1`-`x4` take params with the return value in `x1`,
`x5`-`x6` are callee-saved, `x7` is the stack pointer. It grows down
inside the stack range, see [Memory](#memory). Plain `ld`/`st` with `x7`
as base give direct stack access with offsets.

## Instruction word

Every instruction is 4 byte little-endian word. Low 7 bits hold the
opcode. The rest depends on the format. Register fields are 3 bits wide,
so values 0 to 7.

| Format | Bit fields |
| ------ |:---------- |
| R      | `opcode[6:0]` `rd[9:7]` `rs1[12:10]` `rs2[15:13]` |
| I      | `opcode[6:0]` `rd[9:7]` `rs1[12:10]` `imm[31:13]`, signed 19 bits |
| S      | `opcode[6:0]` `rs1[9:7]` `rs2[12:10]` `imm[31:13]`, signed 19 bits |
| B      | `opcode[6:0]` `rs1[9:7]` `rs2[12:10]` `imm[31:13]` same shape as S, imm counts from current address |
| U      | `opcode[6:0]` `rd[9:7]` `imm[31:12]`, top 20 bits |
| J      | `opcode[6:0]` `rd[9:7]` `imm[31:10]`, signed 22 bits from current address |

| Format | Used for |
| ------ |:-------- |
| R      | Register to register work with up to 3 registers |
| I      | Loads, small constants and single register ops |
| S      | Stores with base from register and signed 19 bit offset |
| B      | Two register comparison and jump relative to current PC with signed 19 bit offset |
| U      | Building big constants |
| J      | Far jumps from current PC |

`call` reuses `J`, `ret` reuses `R` with no operands.

## Opcodes

How to read the semantics column:
`R[x]` is integer register `x`,
`F[f]` is float register `f`,
`M[a:n]` is `n` bytes at address `a`,
`S64`/`S32` are signed views,
`sext` sign-extends,
`PC` is the current address,
`STACK` is the memory-backed value stack.
All integer arithmetic wraps modulo 2^64.

| Opcode | Mnemonic | Format | Operands          | Semantics | Stands for |
| ------:|:-------- | ------ |:----------------- | --------- | ---------- |
| `0x00` | `hlt`    | I      | `imm`             | `exit(imm)` | halt |
| `0x01` | `outi`   | I      | `rs1`             | `print(S64(R[rs1]))` | output integer |
| `0x02` | `outf`   | I      | `rs1`             | `print(F[rs1])` | output float |
| `0x03` | `ld`     | I      | `rd`  `rs1` `imm` | `R[rd] ← M[R[rs1]+imm:8]` | load doubleword |
| `0x04` | `lw`     | I      | `rd`  `rs1` `imm` | `R[rd] ← sext(M[R[rs1]+imm:4])` | load word |
| `0x05` | `st`     | S      | `rs1` `rs2` `imm` | `M[R[rs1]+imm:8] ← R[rs2]` | store doubleword |
| `0x06` | `sw`     | S      | `rs1` `rs2` `imm` | `M[R[rs1]+imm:4] ← R[rs2][31:0]` | store word |
| `0x07` | `fld`    | I      | `rd`  `rs1` `imm` | `F[rd] ← M[R[rs1]+imm:8]` | float load doubleword |
| `0x08` | `fst`    | S      | `rs1` `rs2` `imm` | `M[R[rs1]+imm:8] ← F[rs2]` | float store doubleword |
| `0x09` | `pushqr` | I      | `rs1`             | `STACK.push(R[rs1])` | push quadword from integer register |
| `0x0A` | `pushqv` | I      | `rs1`             | `STACK.push(F[rs1])` | push quadword from float register |
| `0x0B` | `popqr`  | I      | `rd`              | `R[rd] ← STACK.pop()` | pop quadword to integer register |
| `0x0C` | `popqv`  | I      | `rd`              | `F[rd] ← STACK.pop()` | pop quadword to float register |
| `0x0D` | `jal`    | J      | `rd`  `imm`       | `R[rd] ← PC+4; PC ← PC+imm` | jump and link |
| `0x0E` | `beq`    | B      | `rs1` `rs2` `imm` | `if S64(R[rs1])==S64(R[rs2]): PC ← PC+imm` | branch if equal |
| `0x0F` | `bne`    | B      | `rs1` `rs2` `imm` | `if S64(R[rs1])!=S64(R[rs2]): PC ← PC+imm` | branch if not equal |
| `0x10` | `blt`    | B      | `rs1` `rs2` `imm` | `if S64(R[rs1])<S64(R[rs2]): PC ← PC+imm` | branch if less than |
| `0x11` | `bge`    | B      | `rs1` `rs2` `imm` | `if S64(R[rs1])>=S64(R[rs2]): PC ← PC+imm` | branch if greater or equal |
| `0x12` | `add`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← R[rs1]+R[rs2]` | add |
| `0x13` | `sub`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← R[rs1]-R[rs2]` | subtract |
| `0x14` | `mul`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← R[rs1]*R[rs2]` | multiply |
| `0x15` | `div`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← sdiv(R[rs1],R[rs2])`, see edge rules | divide |
| `0x16` | `mod`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← srem(R[rs1],R[rs2])`, see edge rules | modulus |
| `0x17` | `and`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← R[rs1]&R[rs2]` | bitwise AND |
| `0x18` | `or`     | R      | `rd`  `rs1` `rs2` | `R[rd] ← R[rs1]\|R[rs2]` | bitwise OR |
| `0x19` | `xor`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← R[rs1]^R[rs2]` | bitwise XOR |
| `0x1A` | `sll`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← R[rs1]<<(R[rs2]&63)` | shift left logical |
| `0x1B` | `srl`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← R[rs1]>>(R[rs2]&63)`, zero fill | shift right logical |
| `0x1C` | `sra`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← S64(R[rs1])>>(R[rs2]&63)`, sign fill | shift right arithmetic |
| `0x1D` | `addi`   | I      | `rd`  `rs1` `imm` | `R[rd] ← R[rs1]+sext(imm)` | add immediate |
| `0x1E` | `lui`    | U      | `rd`  `imm`       | `R[rd] ← imm<<12` | load upper immediate |
| `0x1F` | `fadd`   | R      | `rd`  `rs1` `rs2` | `F[rd] ← F[rs1]+F[rs2]` | float add |
| `0x20` | `fsub`   | R      | `rd`  `rs1` `rs2` | `F[rd] ← F[rs1]-F[rs2]` | float subtract |
| `0x21` | `fmul`   | R      | `rd`  `rs1` `rs2` | `F[rd] ← F[rs1]*F[rs2]` | float multiply |
| `0x22` | `fdiv`   | R      | `rd`  `rs1` `rs2` | `F[rd] ← F[rs1]/F[rs2]` | float divide |
| `0x23` | `fsqrt`  | R      | `rd`  `rs1`       | `F[rd] ← sqrt(F[rs1])` | float square root |
| `0x25` | `feq`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← (F[rs1]==F[rs2])?1:0`, NaN never equals | float compare equal |
| `0x26` | `flt`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← (F[rs1]<F[rs2])?1:0` | float compare less than |
| `0x27` | `fle`    | R      | `rd`  `rs1` `rs2` | `R[rd] ← (F[rs1]<=F[rs2])?1:0` | float compare less or equal |
| `0x28` | `ecall`  | R      |                   | `host(R[x1])`, see table below | environment call |
| `0x29` | `call`   | J      | `imm`             | `STACK.push(PC+4); PC ← PC+imm` | call |
| `0x2A` | `ret`    | R      |                   | `PC ← STACK.pop()`, trap on underflow/misalign | return |
| `0x2B` | `ini`    | I      | `rd`              | `R[rd] ← parse_int(stdin_line)` | input integer |
| `0x2C` | `inf`    | I      | `rd`              | `F[rd] ← parse_double(stdin_line)` | input float |

Loads read from base register plus signed offset. Stores write to base
plus offset the same way.

`ini`/`inf` read one stdin line each (decimal or hex integers for `ini`,
doubles for `inf`, surrounding whitespace is fine). Output is flushed
before blocking on input. EOF, overlong lines and unparsable text are
fatal, same as bad memory.

Integer divide by zero gives all bits set. Remainder by zero gives back
the dividend. Most negative divided by minus one gives most negative
with zero remainder. This matches RISC-V and keeps behavior defined for
every input.

`call`/`ret` use the value stack, not a link register. `jal` stays for
local jumps with link. Underflow on `ret` and misaligned targets are
fatal, same as `popqr`/`popqv`.

## Narrow loads and stores

`ld`/`st` move whole 8 byte registers. `lw`/`sw` move 4 bytes:

- `lw rd, rs1, imm` reads 4 bytes at `R[rs1]+imm` and sign-extends the
  32 bit value into 64 bits.
- `sw rs1, rs2, imm` writes the low 4 bytes of `R[rs2]` and leaves the
  other 4 bytes of `M[a:8]` alone: `M[a:4] ← R[rs2][31:0]`. It never
  widens, so packing two 32 bit values side by side works.
- `fld`/`fst` always move 8 bytes raw between memory and float
  registers, no conversion. There are no narrow float ops.

No alignment is required: unaligned addresses work the same.
Code, data words and stack slots are aligned in practice, so loads and
stores compile to direct dereferences. Sign extension only happens on
`lw`, never on stores.

## Building constants

One word can not hold 64 bits. Small numbers use `addi` straight away.
Bigger ones stack `lui` plus `addi` to reach 32 bits. Full 64 bit values
live in the data part of the program file and come in through `ld`. The
`x0` register keeps address math simple, since offset from zero is an
absolute address.

## Host calls

`ecall` carries its number in `x1`. Arguments ride in registers
`x2` to `x4`. The return value lands in register `x1`.

| Number | Name    | Arguments    | Description |
| ------:|:------- |:------------ | ----------- |
|    `0` | `exit`  | code in `x2` | End the program with `code & 0xFF` |
|    `1` | `write` | fd `x2`, buf `x3`, len `x4` | Write `len` bytes from `buf` to fd, return bytes written in `x1` |

Write only allows standard output (`1`) and standard error (`2`).
Other file numbers are fatal. The address range must already be mapped,
no silent growth here. Host errors come back as they are.

## Memory

One flat byte array for code, data and stack. The program image starts
at zero and grows on demand when loads or stores reach past the end,
filled with zero. Growth stops at `mem_max` (default 1 GiB,
`--mem-max` overrides in MiB, zero struct field means default).
Anything past that is fatal. Code living in writable memory means self
changing programs work with no extra effort. There is no cache to flush
since nothing runs natively.

The stack is the top region of the same array, carved after the program
image on load (`--stack-size` in KiB, default 64). `x7` points at the
first free slot above the top item and grows down in 8 byte steps.
Overflow past the base, underflow past the top and misaligned `sp` are
fatal. Pushes and pops are bounds-checked memory writes and reads, so
stack slots are ordinary memory: `ld x3, x7, 0` peeks the top without
popping, `ld x4, x7, 8` reads deeper. Negative offsets work because the
address wraps modulo 2^64 (`R[x7]-8` for `-8`).

RISC-V has no dedicated stack ops either: `sp` plus `ld`/`st`/`addi`
is the convention there too. Our `pushqr`/`pushqv`/`popqr`/`popqv` plus
`call`/`ret` are conveniences over the same idea.

## Program files

Files start with a tiny header that mirrors ELF field names
(`WstProgHeader`/`WstProgSegment` in `toys/prog.h`). Magic bytes
are `7f 57 53 54`, version is `1`. The header points at an array of
segments and names the entry offset. The assembler links one text
segment at address zero and one data segment right after the code
(first 8-aligned address past it, `wst_prog_data_addr`). Each segment
tells file offset, target address, file size and memory size. The
loader checks the whole file before touching memory. Magic, version,
header bounds, segment bounds, address cap, overlapping segments and a
sane aligned entry all get verified first. Then segments copy over and
the gap between file size and memory size stays zero, which gives free
zeroed data. The stack is not in the file: it is appended in memory
after load.

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
```

- Code lines are `mnemonic operands...` with `x0`-`x7`/`f0`-`f7`
  registers and decimal or hex numbers, or a `:label` definition alone
  on a line. Labels address the start of the next instruction.
- Data entries are `name: <type> <value>` on one line. Types are
  `u64`/`u32` (numbers), `f64` (double) and `str "..."` with `\n`,
  `\\`, `\"`, `\0`, `\t` escapes. `u64`/`f64` align to 8 bytes,
  `u32` to 4, strings pack exact bytes with no NUL added.
- Code and data share one namespace: redefining a name is an error.
- A `:name` operand refers to a label or a data entry. First pass
  emits words and records every `:name` as a fixup, second pass
  patches the machine code once code length (therefore, data addresses)
  is known. `B`/`J` immediates (`beq`, `jal`, `call`) are PC-relative
  (`target - ins_addr`), `I`/`S`/`U` immediates (`ld`, `addi`, `st`,
  `lui`) are absolute addresses. `lui` needs a 4096-aligned address.
  Unknown names and out-of-range immediates are errors.

The disassembler (`toys/disasm.h`, `asm --disasm`) prints one
`mnemonic operands` line per word with numeric immediates, so its
output re-assembles to identical code bytes (data blobs are code-only:
names and data do not survive disassembly).

## Example program

This program prints 20 and 6.5, then ends. Integers come from `addi`.
Doubles come from the data segment through `fld`. Source is
`examples/vm/prog.asm`, built with `asm prog.asm prog.bin`.

```
section .text
addi x1, x0, 2
addi x5, x0, 3
add  x3, x1, x5
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
```
