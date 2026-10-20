# Machine memory map and boot

Guest memory is one flat zeroed byte array reserved once on load,
`mem_size` bytes (default 256 MiB, `--mem-max` in MiB, zero means
default). It never grows. Every access past the reservation is fatal.
The instruction set is in [isa](isa.md).

```
low         loaded image, code then data
...         free zeroed memory
...         WstVmConfig handoff block
...         framebuffer, when enabled
...         stack, growing down
top         end of guest memory
```

Config, framebuffer and stack addresses depend on `mem_size`, the
stack size and the framebuffer size. The loaded image must end below
the config block.

## Stack

The stack is the top region of guest memory, `stack.size` bytes
(`--stack-size` in KiB, default 64). `x7` points at the first free
slot and grows down in 8 byte steps. Overflow past the base,
underflow past the top and misaligned `sp` are fatal. Slots are
ordinary memory: `ld x3, x7, 0` peeks the top without popping, and
negative offsets wrap modulo 2^64.

## Boot state

`wst_vm_init` sets `pc` to the entry, `x7` to the stack top and `x1`
to the config block. Every other register is zero.

## Config handoff

`WstVmConfig` is copied into guest memory on load:

| Offset | Size | Field |
| ------:| ----:| ----- |
|      0 |    4 | magic, `WSTF` |
|      4 |    4 | version, `1` |
|      8 |    4 | framebuffer width |
|     12 |    4 | framebuffer height |
|     16 |    8 | framebuffer guest address |
|     24 |    4 | framebuffer row stride in bytes |
|     28 |    4 | pixel format |
|     32 |    8 | whole guest memory size |
|     40 |    8 | first byte above the stack |

Width and height are zero when the framebuffer is off.

Possible pixel formats:

| Number | Format   |
| ------:| -------- |
|      1 | RGBA8888 |
