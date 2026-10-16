#define ASM_TYPES_IMPL
#include "asm_priv.h"
#undef ASM_TYPES_IMPL

#include "toys/debug.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>


bool asm_sym_find(const AsmCtx *ctx, const char *name, AsmSym *out)
{
    assert(ctx != NULL);
    assert(name != NULL);
    assert(out != NULL);

    for (size_t i = 0; i < ctx->syms.length; i++) {
        AsmSym sym;
        wst_vec_sym_get(&ctx->syms, i, &sym);
        if (strcmp(sym.name, name) == 0) {
            *out = sym;
            return true;
        }
    }
    return false;
}

bool asm_sym_add(AsmCtx *ctx, const char *name, size_t addr,
                 AsmSymType type, size_t lineno)
{
    assert(ctx != NULL);
    assert(name != NULL);

    AsmSym existing;
    if (asm_sym_find(ctx, name, &existing)) {
        LOG_E("line %zu: duplicate symbol '%s'", lineno, name);
        return false;
    }

    char *dup = strdup(name);
    if (dup == NULL) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }

    AsmSym sym = { .name = dup, .addr = addr, .type = type };
    WstVecErr err = wst_vec_sym_push(&ctx->syms, &sym);
    if (err != WST_VEC_NO_ERR) {
        LOG_E("line %zu: %s", lineno, wst_vec_err_str(err));
        free(dup);
        return false;
    }
    return true;
}

bool asm_fix_add(AsmCtx *ctx, size_t off, WstVmInstrFormat fmt,
                 const char *name, size_t lineno)
{
    assert(ctx != NULL);
    assert(name != NULL);

    char *dup = strdup(name);
    if (dup == NULL) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }

    AsmFixup fix = { .off = off, .fmt = fmt, .name = dup, .lineno = lineno };
    WstVecErr err = wst_vec_fixup_push(&ctx->fixups, &fix);
    if (err != WST_VEC_NO_ERR) {
        LOG_E("line %zu: %s", lineno, wst_vec_err_str(err));
        free(dup);
        return false;
    }
    return true;
}

void asm_tables_free(AsmCtx *ctx)
{
    assert(ctx != NULL);

    for (size_t i = 0; i < ctx->syms.length; i++) {
        AsmSym sym;
        wst_vec_sym_get(&ctx->syms, i, &sym);
        free(sym.name);
    }
    for (size_t i = 0; i < ctx->fixups.length; i++) {
        AsmFixup fix;
        wst_vec_fixup_get(&ctx->fixups, i, &fix);
        free(fix.name);
    }

    WstVecErr err_syms = wst_vec_sym_free(&ctx->syms);
    WstVecErr err_fixups = wst_vec_fixup_free(&ctx->fixups);
    if (err_syms != WST_VEC_NO_ERR || err_fixups != WST_VEC_NO_ERR)
        LOG_W("symbol tables damaged: %s / %s",
              wst_vec_err_str(err_syms), wst_vec_err_str(err_fixups));
}
