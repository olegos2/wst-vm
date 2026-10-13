#ifndef TOYS_ASM_H
#define TOYS_ASM_H

#include "toys/vec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Assemble one code line (`mnemonic operands...`) into a 4-byte word.
 * Operands are `x0`-`x7`/`f0`-`f7` registers and decimal or hex numbers.
 * Unknown mnemonics, bad operands and out-of-range immediates are errors.
 *
 * Single-line form only, labels are resolved by `wst_vm_asm_source`.
 * A label operand (`:name`) is an error here, it needs the two-pass fixup.
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_vm_asm_line(const char *line, size_t lineno, uint32_t *out);

/**
 * Assemble whole source text into machine code bytes, appended to `out`.
 * Blank lines and `#` comments are skipped, `lineno` counts every line.
 *
 * Two passes over `src`:
 * - first pass emits words and records `:label` definitions with their
 *   byte addresses in a label table (linear array of exact strings,
 *   djb2 hashing only if programs grow past hundreds of labels);
 * - second pass patches label operands that were unknown on first pass.
 *
 * Line forms: empty line, `mnemonic operands...`, `:label_name`.
 * A separate `wst_prog_save`-style step turns these bytes plus entry
 * into a program file, it does not belong here.
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_vm_asm_source(const char *src, size_t len, WstVecU8 *out);

#endif /* TOYS_ASM_H */
