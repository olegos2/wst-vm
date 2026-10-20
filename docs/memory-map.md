# Machine memory map and boot

The vm loads the image into zeroed guest memory and passes a
`WstVmConfig` block to it. This is what the image sees. The
instruction set is in [isa](isa.md).

## Map

```
low         loaded image, code then data
...         free zeroed memory
...         WstVmConfig handoff block
...         framebuffer, when enabled
...         stack, growing down
top         end of guest memory, `mem_size`
```

The config, framebuffer and stack addresses depend on `mem_max`,
`stack.size` and the framebuffer size. The loaded image must end
below the config block.

## Guest memory

One flat byte array for code, data and stack. It is reserved once
on load, `mem_max` bytes, zeroed, default 256 MiB (`--mem-max` in
MiB, zero means default). Nothing ever grows it, every load, store
or fetch past the reservation is fatal. Code lives in writable
memory, so self changing programs work with no extra effort. There
is no cache to flush since nothing runs natively.

## Stack

The stack is the top region of the same array, `stack_size` bytes
(`--stack-size` in KiB, default 64). `x7` points at the first free
slot above the top item and grows down in 8 byte steps. Overflow
past the base, underflow past the top and misaligned `sp` are fatal.

Pushes and pops are bounds-checked memory writes and reads, so stack
slots are ordinary memory: `ld x3, x7, 0` peeks the top without
popping, `ld x4, x7, 1` reads deeper. Negative offsets work because
the address wraps modulo 2^64 (`R[x7]-8` for `-8`).

## Boot state

`pc` holds the entry address, `x7` the stack top and `x1` the
address of `WstVmConfig`. Every other register reads as zero.

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
