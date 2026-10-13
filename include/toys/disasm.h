#ifndef TOYS_DISASM_H
#define TOYS_DISASM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Format one instruction word as `mnemonic operands` into `buf`.
 * Immediates print as signed decimal, output re-assembles.
 * Unknown opcodes are an error, not data: use `.word` yourself.
 *
 * @return `true` on success, `false` on unknown opcode or short buffer.
 */
bool wst_vm_disasm_one(uint32_t word, char *buf, size_t cap);

#endif /* TOYS_DISASM_H */
