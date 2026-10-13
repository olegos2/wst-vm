#include "toys/asm.h"
#include "asm_priv.h"
#include "toys/debug.h"
#include "toys/prog.h"
#include "toys/vec_internal.h"
#include "toys/vm.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


void wst_vm_asm_object_free(WstAsmObject *obj)
{
    if (obj == NULL)
        return;

    WstVecErr e1 = wst_vec_u8_free(&obj->code);
    WstVecErr e2 = wst_vec_u8_free(&obj->data);
    if (e1 != WST_VEC_NO_ERR || e2 != WST_VEC_NO_ERR)
        LOG_W("object damaged: %s / %s", wst_vec_err_str(e1), wst_vec_err_str(e2));
}

/* `#` starts a comment unless inside a double-quoted string. */
static void asm_strip_comment(char *line)
{
    bool in_str = false;
    for (char *p = line; *p != '\0'; p++) {
        if (in_str) {
            if (*p == '\\' && p[1] != '\0')
                p++;
            else if (*p == '"')
                in_str = false;
        } else if (*p == '"') {
            in_str = true;
        } else if (*p == '#') {
            *p = '\0';
            break;
        }
    }
}

static bool asm_section(AsmCtx *ctx, const char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);

    const char *name = asm_skip_ws(line + strlen("section"));
    if (strcmp(name, ".text") == 0) {
        ctx->in_data = false;
        return true;
    }
    if (strcmp(name, ".data") == 0) {
        ctx->in_data = true;
        return true;
    }

    LOG_E("line %zu: unknown section '%s' (sections: .text, .data)",
          lineno, name);
    return false;
}

static bool asm_label(AsmCtx *ctx, const char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);

    if (ctx->in_data) {
        LOG_E("line %zu: labels belong in section .text", lineno);
        return false;
    }

    const char *name = line + 1;
    if (!asm_valid_name(name)) {
        LOG_E("line %zu: bad label name", lineno);
        return false;
    }

    return asm_sym_add(ctx, name, ctx->obj->code.length, ASM_SYM_LABEL, lineno);
}

static bool asm_text_line(AsmCtx *ctx, char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);

    if (ctx->in_data) {
        LOG_E("line %zu: instructions belong in section .text", lineno);
        return false;
    }

    size_t off = ctx->obj->code.length;
    uint32_t word = 0;
    char *sym = NULL;
    WstVmInstrFormat fmt = WST_VM_INSTR_FORMAT_INVALID;
    if (!asm_parse_insn(line, lineno, true, &word, &sym, &fmt))
        return false;

    if (!asm_emit_word(ctx, word, lineno)) {
        free(sym);
        return false;
    }
    if (sym != NULL) {
        bool ok = asm_fix_add(ctx, off, fmt, sym, lineno);
        free(sym);
        if (!ok)
            return false;
    }
    return true;
}

static bool asm_source_line(AsmCtx *ctx, char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);

    const char *p = asm_skip_ws(line);
    if (*p == '\0')
        return true;

    if (strncmp(p, "section", 7) == 0 && (p[7] == '\0' || asm_is_ws(p[7])))
        return asm_section(ctx, line, lineno);
    if (p[0] == ':')
        return asm_label(ctx, p, lineno);
    if (ctx->in_data) {
        if (strchr(p, ':') == NULL) {
            LOG_E("line %zu: instructions belong in section .text", lineno);
            return false;
        }
        return asm_data_entry(ctx, (char *)p, lineno);
    }

    /* `name:` outside .data is a misplaced data entry, not an insn. */
    size_t nlen = strspn(p, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_");
    if (p[nlen] == ':') {
        LOG_E("line %zu: data entries belong in section .data", lineno);
        return false;
    }
    return asm_text_line(ctx, (char *)p, lineno);
}

/* Second pass: every `:name` becomes an absolute or PC-relative imm. */
static bool asm_resolve(AsmCtx *ctx)
{
    assert(ctx != NULL);

    size_t data_base = wst_prog_data_addr(ctx->obj->code.length);
    for (size_t i = 0; i < ctx->fixups.len; i++) {
        const AsmFixup *fix = &ctx->fixups.items[i];
        const AsmSym *sym = asm_sym_find(ctx, fix->name);
        if (sym == NULL) {
            LOG_E("line %zu: unknown symbol '%s'", fix->lineno, fix->name);
            return false;
        }

        size_t target = sym->addr;
        if (sym->type == ASM_SYM_DATA) {
            if (target > SIZE_MAX - data_base) {
                LOG_E("line %zu: symbol '%s' address overflows",
                      fix->lineno, fix->name);
                return false;
            }
            target = data_base + target;
        }

        int64_t imm = 0;
        if (fix->fmt == WST_VM_INSTR_FORMAT_B ||
            fix->fmt == WST_VM_INSTR_FORMAT_J)
            imm = (int64_t)target - (int64_t)fix->off;
        else
            imm = (int64_t)target;

        if (!asm_check_imm(fix->fmt, imm, fix->lineno))
            return false;

        uint32_t word = 0;
        memcpy(&word, &ctx->obj->code.data[fix->off], sizeof(word));
        switch (fix->fmt) {
        case WST_VM_INSTR_FORMAT_I:
        case WST_VM_INSTR_FORMAT_S:
        case WST_VM_INSTR_FORMAT_B:
            word = asm_set_imm19(word, imm);
            break;
        case WST_VM_INSTR_FORMAT_J:
            word = asm_set_imm22(word, imm);
            break;
        case WST_VM_INSTR_FORMAT_U:
            word = asm_set_imm20(word, imm);
            break;
        default:
            LOG_E("line %zu: bad fixup format", fix->lineno);
            return false;
        }
        memcpy(&ctx->obj->code.data[fix->off], &word, sizeof(word));
    }
    VEC_SEAL((WstVecVoid *)&ctx->obj->code, sizeof(uint8_t));
    return true;
}

bool wst_vm_asm_line(const char *line, size_t lineno, uint32_t *out)
{
    assert(line != NULL);
    assert(out != NULL);

    char *tmp = strdup(line);
    if (tmp == NULL) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }

    asm_strip_comment(tmp);

    uint32_t word = 0;
    char *sym = NULL;
    WstVmInstrFormat fmt = WST_VM_INSTR_FORMAT_INVALID;
    bool ok = asm_parse_insn(tmp, lineno, false, &word, &sym, &fmt);
    free(sym);
    if (ok)
        *out = word;

    free(tmp);
    return ok;
}

bool wst_vm_asm_source(const char *src, size_t len, WstAsmObject *out)
{
    assert(src != NULL);
    assert(out != NULL);

    wst_vec_u8_free(&out->code);
    wst_vec_u8_free(&out->data);

    char *copy = malloc(len + 1);
    if (copy == NULL) {
        LOG_E("out of memory");
        return false;
    }
    memcpy(copy, src, len);
    copy[len] = '\0';

    AsmCtx ctx = {
        .obj = out,
        .in_data = false,
    };

    bool ok = true;
    size_t lineno = 0;
    char *pos = copy;
    while (*pos != '\0') {
        char *nl = strchr(pos, '\n');
        size_t llen = nl != NULL ? (size_t)(nl - pos) : strlen(pos);
        if (llen > 0 && pos[llen - 1] == '\r')
            llen--;
        pos[llen] = '\0';
        lineno++;

        asm_strip_comment(pos);
        if (!asm_source_line(&ctx, pos, lineno)) {
            ok = false;
            break;
        }
        pos += llen + (nl != NULL ? 1 : 0);
    }

    if (ok)
        ok = asm_resolve(&ctx);

    asm_tables_free(&ctx);
    free(copy);

    if (!ok) {
        wst_vec_u8_free(&out->code);
        wst_vec_u8_free(&out->data);
    }
    return ok;
}
