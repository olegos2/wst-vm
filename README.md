# wst-vm

Tiny virtual machine: interpreter, assembler and disassembler.
Fixed 4 byte instructions, RISC-V inspired ISA, ELF-like binaries.

ISA, registers, syscalls and assembly syntax: [docs/isa.md](docs/isa.md).

## What you need

- A C compiler, `meson >= 1.1.0`, `ninja`
- `raylib` for the vm framebuffer window

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

Circle drawn into software framebuffer:

```sh
./builddir/examples/asm/asm examples/vm/circle.asm circle.bin
./builddir/examples/vm/vm --fb --fb-scale 2 circle.bin
```

Mandelbulb raymarcher:

```sh
./builddir/examples/asm/asm examples/vm/mandelbulb.asm mandelbulb.bin
./builddir/examples/vm/vm --fb --fb-width 160 --fb-height 120 --fb-scale 2 mandelbulb.bin
```

C reference for the same frame (`examples/vm/mandelbulb_ref.c`):

```sh
./builddir/examples/vm/mandelbulb_ref 160 120 mandelbulb.ppm
```
