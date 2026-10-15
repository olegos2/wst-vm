#ifndef TOYS_ASM_H
#define TOYS_ASM_H

#include "toys/vec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Assembled program before linking, code words plus data bytes.
 * Data addresses are data segment relative in code.
 * `entry` is the `:_start` address, zero when the source has none.
 * `wst_prog_save` links code and data into a program file with entry.
 */
typedef struct {
    WstVecU32 code;
    WstVecU8 data;
    size_t entry;
} WstAsmObject;

/**
 * Free an assembly object.
 */
void wst_vm_asm_object_free(WstAsmObject *obj);

/**
 * Assemble one code line (`mnemonic operands...`) into a 4-byte word.
 * Operands are `x0`-`x7`, `f0`-`f7` registers and decimal or hex numbers.
 * Unknown mnemonics, bad operands and out of range immediates are errors.
 *
 * A label operand (`:name`) is an error here.
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_vm_asm_line(const char *line, size_t lineno, uint32_t *out);

/**
 * Assemble whole source text into `out`.
 * Resets `out`, freeing previous content. Fails with `out` left empty.
 *
 * Sections, one per line, default is `.text`:
 * - `section .text`: code, `:label` definitions and instructions.
 * - `section .data`: `name: <type> <value>` entries, types are
 *   `u64`/`u32` (dec/hex int), `f64` (double) and
 *   `str "..."` (no NUL added automatically).
 *   `u64`/`f64` entries align to 8, `u32` to 4.
 *
 * Line forms in `.text`: empty line, `mnemonic operands...`, `:label`.
 * `#` comments are stripped, blank lines are skipped.
 *
 * Two passes: first pass emits words and records labels with data
 * names and their offsets. Second pass resolves every `:name` operand.
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_vm_asm_source(const char *src, size_t len, WstAsmObject *out);

#endif /* TOYS_ASM_H */
