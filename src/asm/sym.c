#define ASM_TYPES_IMPL
#include "asm_priv.h"
#undef ASM_TYPES_IMPL

#include "toys/debug.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>


const AsmSym *asm_sym_find(const AsmCtx *ctx, const char *name)
{
    assert(ctx != NULL);
    assert(name != NULL);

    for (size_t i = 0; i < ctx->syms.length; i++) {
        if (strcmp(ctx->syms.data[i].name, name) == 0)
            return &ctx->syms.data[i];
    }
    return NULL;
}

bool asm_sym_add(AsmCtx *ctx, const char *name, size_t addr,
                 AsmSymType type, size_t lineno)
{
    assert(ctx != NULL);
    assert(name != NULL);

    if (asm_sym_find(ctx, name) != NULL) {
        LOG_E("line %zu: duplicate symbol '%s'", lineno, name);
        return false;
    }

    char *dup = strdup(name);
    if (dup == NULL) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }

    AsmSym sym = { .name = dup, .addr = addr, .type = type };
    WstVecErr err = asm_symvec_push(&ctx->syms, &sym);
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
    WstVecErr err = asm_fixupvec_push(&ctx->fixups, &fix);
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

    for (size_t i = 0; i < ctx->syms.length; i++)
        free(ctx->syms.data[i].name);
    for (size_t i = 0; i < ctx->fixups.length; i++)
        free(ctx->fixups.data[i].name);

    WstVecErr err_syms = asm_symvec_free(&ctx->syms);
    WstVecErr err_fixups = asm_fixupvec_free(&ctx->fixups);
    if (err_syms != WST_VEC_NO_ERR || err_fixups != WST_VEC_NO_ERR)
        LOG_W("symbol tables damaged: %s / %s",
              wst_vec_err_str(err_syms), wst_vec_err_str(err_fixups));
}
