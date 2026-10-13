#ifndef TOYS_ASM_H
#define TOYS_ASM_H

#include "toys/vec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Assembled program before linking with code and data blob.
 * Data addresses are data segment relative in code.
 * `wst_prog_save` links the two into a program file.
 */
typedef struct {
    WstVecU8 code;
    WstVecU8 data;
} WstAsmObject;

/**
 * Free an assembly object.
 */
void wst_vm_asm_object_free(WstAsmObject *obj);

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
 * Assemble whole source text (`src` of `len` bytes) into `out`.
 * Resets `out`, freeing previous content. Fails with `out` left empty.
 *
 * Sections, one per line (default at start is `section .text`):
 * - `section .text`: code, `:label` definitions and instructions.
 * - `section .data`: `name: <type> <value>` entries, types are
 *   `u64`/`u32` (dec/hex int), `f64` (double) and
 *   `str "..."` (no NUL added automatically).
 *   `u64`/`f64` entries align to 8, `u32` to 4.
 *
 * Line forms in `.text`: empty line, `mnemonic operands...`, `:label`.
 * Blank lines and `#` comments are skipped, `lineno` counts every line.
 *
 * Two passes: first pass emits words and records labels plus data
 * names with their offsets (linear table of exact strings, djb2
 * hashing only if programs grow past hundreds of symbols). Second
 * pass resolves every `:name` operand: B/J immediates are PC-relative
 * (`target - ins_addr`), I/S/U immediates are absolute data/label
 * addresses (`lui` needs a 4096-aligned address).
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_vm_asm_source(const char *src, size_t len, WstAsmObject *out);

#endif /* TOYS_ASM_H */
