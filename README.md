# wst-vm

Tiny virtual machine: interpreter, assembler and disassembler.
Fixed 4 byte instructions, RISC-V inspired ISA, ELF-like binaries.

ISA, registers, syscalls and assembly syntax: [docs/isa.md](docs/isa.md).

## What you need

- A C compiler, `meson >= 1.1.0`, `ninja`

## How to build

```sh
meson setup builddir
meson compile -C builddir
```

Depends on `wst-toys` subproject.

## How to run

```sh
./builddir/examples/asm/asm examples/vm/prog.asm prog.bin
./builddir/examples/vm/vm prog.bin
```
