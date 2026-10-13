#include "asm_priv.h"
#include "toys/debug.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/* Grow a table buffer, doubling from 16. Shared by symbols and fixups. */
static bool asm_grow(void **items, size_t *cap, size_t len, size_t elem)
{
    assert(items != NULL);
    assert(cap != NULL);

    if (len < *cap)
        return true;

    size_t ncap = *cap == 0 ? 16 : *cap * 2;
    if (ncap < *cap || (elem != 0 && ncap > SIZE_MAX / elem)) {
        LOG_E("table too large");
        return false;
    }

    void *next = realloc(*items, ncap * elem);
    if (next == NULL) {
        LOG_E("out of memory");
        return false;
    }
    *items = next;
    *cap = ncap;
    return true;
}

const AsmSym *asm_sym_find(const AsmCtx *ctx, const char *name)
{
    assert(ctx != NULL);
    assert(name != NULL);

    for (size_t i = 0; i < ctx->syms.len; i++) {
        if (strcmp(ctx->syms.items[i].name, name) == 0)
            return &ctx->syms.items[i];
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

    if (!asm_grow((void **)&ctx->syms.items, &ctx->syms.cap,
                  ctx->syms.len, sizeof(*ctx->syms.items)))
        return false;

    char *dup = strdup(name);
    if (dup == NULL) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }

    ctx->syms.items[ctx->syms.len].name = dup;
    ctx->syms.items[ctx->syms.len].addr = addr;
    ctx->syms.items[ctx->syms.len].type = type;
    ctx->syms.len++;
    return true;
}

bool asm_fix_add(AsmCtx *ctx, size_t off, WstVmInstrFormat fmt,
                 const char *name, size_t lineno)
{
    assert(ctx != NULL);
    assert(name != NULL);

    if (!asm_grow((void **)&ctx->fixups.items, &ctx->fixups.cap,
                  ctx->fixups.len, sizeof(*ctx->fixups.items)))
        return false;

    char *dup = strdup(name);
    if (dup == NULL) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }

    ctx->fixups.items[ctx->fixups.len].off = off;
    ctx->fixups.items[ctx->fixups.len].fmt = fmt;
    ctx->fixups.items[ctx->fixups.len].name = dup;
    ctx->fixups.items[ctx->fixups.len].lineno = lineno;
    ctx->fixups.len++;
    return true;
}

void asm_tables_free(AsmCtx *ctx)
{
    assert(ctx != NULL);

    for (size_t i = 0; i < ctx->syms.len; i++)
        free(ctx->syms.items[i].name);
    free(ctx->syms.items);
    ctx->syms.items = NULL;
    ctx->syms.len = 0;
    ctx->syms.cap = 0;

    for (size_t i = 0; i < ctx->fixups.len; i++)
        free(ctx->fixups.items[i].name);
    free(ctx->fixups.items);
    ctx->fixups.items = NULL;
    ctx->fixups.len = 0;
    ctx->fixups.cap = 0;
}
