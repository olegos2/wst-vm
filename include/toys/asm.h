#ifndef TOYS_ASM_H
#define TOYS_ASM_H

#include "toys/vec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/**
 * Assembled program object before linking. Data addresses are data
 * segment relative in code. `entry` is the `:_start` address or zero.
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
 * Assemble one code line into a 4-byte word. Labels are not allowed.
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_vm_asm_line(const char *line, size_t lineno, uint32_t *out);

/**
 * Assemble whole source text into `out`, resetting it first and
 * leaving it empty on failure.
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_vm_asm_source(const char *src, size_t len, WstAsmObject *out);

/**
 * Write a program file as assembly text to `out`. A cut by `max_words`
 * skips the data segment.
 *
 * @param out stream to print to
 * @param path program file to disassemble
 * @param max_words code words to print, zero for all
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_vm_disasm(FILE *out, const char *path, size_t max_words);

#endif /* TOYS_ASM_H */
