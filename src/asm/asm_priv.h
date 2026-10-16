#ifndef ASM_PRIV_H
#define ASM_PRIV_H

#include "toys/asm.h"
#include "toys/vm.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/** Assembly section, selects the line handler. */
typedef enum {
    ASM_TEXT = 0,
    ASM_DATA,
} AsmSection;

/** Type of symbol in assembly. */
typedef enum {
    ASM_SYM_LABEL,
    ASM_SYM_DATA,
} AsmSymType;

/** Named code address or data offset. */
typedef struct {
    char *name;
    size_t addr;
    AsmSymType type;
} AsmSym;

/** Unresolved `:name` operand, patched once all addresses are known. */
typedef struct {
    size_t off;
    WstVmInstrFormat fmt;
    char *name;
    size_t lineno;
} AsmFixup;

/* Stamped tables, decls everywhere, impls only where ASM_TYPES_IMPL is used. */
#ifdef ASM_TYPES_IMPL
#define VEC_IMPL
#endif
#define VEC_ELEM AsmSym
#define VEC_T WstVecSym
#define VEC_F(name) wst_vec_sym_ ## name
#define VEC_REF
#include "toys/vec_internal.h"

#define VEC_ELEM AsmFixup
#define VEC_T WstVecFixup
#define VEC_F(name) wst_vec_fixup_ ## name
#define VEC_REF
#include "toys/vec_internal.h"
#ifdef ASM_TYPES_IMPL
#undef VEC_IMPL
#endif

/** First-pass state, output buffers, symbol table, fixup list, section. */
typedef struct {
    WstAsmObject *obj;
    WstVecSym syms;
    WstVecFixup fixups;
    AsmSection section;
} AsmCtx;


static inline bool asm_is_ws(char c)
{
    return isspace((unsigned char)c) != 0;
}

static inline const char *asm_skip_ws(const char *p)
{
    while (asm_is_ws(*p))
        p++;
    return p;
}

static inline bool asm_is_name_start(char c)
{
    return isalpha((unsigned char)c) != 0 || c == '_';
}

static inline bool asm_is_name_char(char c)
{
    return isalnum((unsigned char)c) != 0 || c == '_';
}

static inline bool asm_valid_name(const char *name)
{
    if (!asm_is_name_start(name[0]))
        return false;

    for (const char *p = name + 1; *p != '\0'; p++) {
        if (!asm_is_name_char(*p))
            return false;
    }
    return true;
}


/** Look up a symbol by exact name, copy into `out`, false when missing. */
bool asm_sym_find(const AsmCtx *ctx, const char *name, AsmSym *out);

/** Record a label or data name, false on duplicates. */
bool asm_sym_add(AsmCtx *ctx, const char *name, size_t addr,
                 AsmSymType type, size_t lineno);

/** Record one `:name` use to patch later. */
bool asm_fix_add(AsmCtx *ctx, size_t off, WstVmInstrFormat fmt,
                 const char *name, size_t lineno);

/** Free tables and the names in them. */
void asm_tables_free(AsmCtx *ctx);

/*
 * Encode one `mnemonic operands...` line with comments stripped.
 * A `:name` operand comes back in `sym_out`, free it after use.
 */
bool asm_parse_instr(char *line, size_t lineno, bool allow_sym,
                     uint32_t *word_out, char **sym_out,
                     WstVmInstrFormat *fmt_out);

/** Append a word to the code buffer, little-endian. */
bool asm_emit_word(AsmCtx *ctx, uint32_t word, size_t lineno);

/** Reject an immediate too wide for its format. */
bool asm_check_imm(WstVmInstrFormat fmt, int64_t imm, size_t lineno);

/** Patch the imm field of an encoded word. */
uint32_t asm_set_imm19(uint32_t word, int64_t imm);
uint32_t asm_set_imm22(uint32_t word, int64_t imm);
uint32_t asm_set_imm20(uint32_t word, int64_t imm);

/** Parse and emit one `name: <type> <value>` data line. */
bool asm_data_entry(AsmCtx *ctx, char *line, size_t lineno);

#endif /* ASM_PRIV_H */
