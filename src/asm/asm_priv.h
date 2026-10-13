#ifndef ASM_PRIV_H
#define ASM_PRIV_H

#include "toys/asm.h"
#include "toys/vm.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/** Symbol kind: code label address or data blob offset. */
typedef enum {
    ASM_SYM_LABEL,
    ASM_SYM_DATA,
} AsmSymType;

/** Named address, code labels hold bytes, data names hold offsets. */
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

typedef struct {
    AsmSym *items;
    size_t len;
    size_t cap;
} AsmSyms;

typedef struct {
    AsmFixup *items;
    size_t len;
    size_t cap;
} AsmFixups;

/** First-pass state: output buffers, symbol table, fixup list, section. */
typedef struct {
    WstAsmObject *obj;
    AsmSyms syms;
    AsmFixups fixups;
    bool in_data;
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


/* sym.c */
const AsmSym *asm_sym_find(const AsmCtx *ctx, const char *name);
bool asm_sym_add(AsmCtx *ctx, const char *name, size_t addr,
                 AsmSymType type, size_t lineno);
bool asm_fix_add(AsmCtx *ctx, size_t off, WstVmInstrFormat fmt,
                 const char *name, size_t lineno);
void asm_tables_free(AsmCtx *ctx);

/* instr.c */
bool asm_parse_instr(char *line, size_t lineno, bool allow_sym,
                     uint32_t *word_out, char **sym_out,
                     WstVmInstrFormat *fmt_out);
bool asm_emit_word(AsmCtx *ctx, uint32_t word, size_t lineno);
bool asm_check_imm(WstVmInstrFormat fmt, int64_t imm, size_t lineno);
uint32_t asm_set_imm19(uint32_t word, int64_t imm);
uint32_t asm_set_imm22(uint32_t word, int64_t imm);
uint32_t asm_set_imm20(uint32_t word, int64_t imm);

/* data.c */
bool asm_data_entry(AsmCtx *ctx, char *line, size_t lineno);

#endif /* ASM_PRIV_H */
