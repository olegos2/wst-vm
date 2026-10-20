#include "toys/asm.h"
#include "asm_priv.h"
#include "toys/debug.h"
#include "toys/prog.h"
#include "toys/vm.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


void wst_vm_asm_object_free(WstAsmObject *obj)
{
    if (obj == NULL)
        return;

    WstVecErr err_code = wst_vec_u32_free(&obj->code);
    WstVecErr err_data = wst_vec_u8_free(&obj->data);

    if (err_code != WST_VEC_NO_ERR || err_data != WST_VEC_NO_ERR)
        LOG_W("object damaged: code %s, data %s",
              wst_vec_err_str(err_code), wst_vec_err_str(err_data));
}

/* `#` starts a comment unless inside a double-quoted string. */
static void asm_strip_comment(char *line)
{
    bool in_str = false;
    for (char *pos = line; *pos != '\0'; pos++) {
        if (in_str) {
            if (*pos == '\\' && pos[1] != '\0')
                pos++;
            else if (*pos == '"')
                in_str = false;
        } else if (*pos == '"') {
            in_str = true;
        } else if (*pos == '#') {
            *pos = '\0';
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
        ctx->section = ASM_TEXT;
        return true;
    }
    if (strcmp(name, ".data") == 0) {
        ctx->section = ASM_DATA;
        return true;
    }

    LOG_E("line %zu: unknown section '%s' (sections: .text, .data)",
          lineno, name);
    return false;
}

/* Code size in bytes, labels and fixups count bytes, not words. */
static size_t asm_code_bytes(const AsmCtx *ctx)
{
    assert(ctx != NULL);

    return ctx->obj->code.length * WST_VM_INSTR_SIZE;
}

static bool asm_label(AsmCtx *ctx, const char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);
    assert(ctx->section == ASM_TEXT);

    const char *name = line + 1;
    if (!asm_valid_name(name)) {
        LOG_E("line %zu: bad label name", lineno);
        return false;
    }

    return asm_sym_add(ctx, name, asm_code_bytes(ctx), ASM_SYM_LABEL, lineno);
}

static bool asm_text_line(AsmCtx *ctx, char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);
    assert(ctx->section == ASM_TEXT);

    size_t off = asm_code_bytes(ctx);
    uint32_t word = 0;
    char *sym = NULL;
    WstVmInstrFormat fmt = WST_VM_INSTR_FORMAT_INVALID;

    if (!asm_parse_instr(line, lineno, true, &word, &sym, &fmt))
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

/* Handle one `.data` line, labels and code belong in `.text`. */
static bool asm_data_line(AsmCtx *ctx, const char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);
    assert(ctx->section == ASM_DATA);

    if (line[0] == ':') {
        LOG_E("line %zu: labels belong in section .text", lineno);
        return false;
    }

    if (strchr(line, ':') == NULL) {
        LOG_E("line %zu: instructions belong in section .text", lineno);
        return false;
    }

    return asm_data_entry(ctx, (char *)line, lineno);
}

static bool asm_source_line(AsmCtx *ctx, char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);

    const char *pos = asm_skip_ws(line);
    if (*pos == '\0')
        return true;

    if (strncmp(pos, "section", 7) == 0 &&
        (pos[7] == '\0' || asm_is_ws(pos[7])))
        return asm_section(ctx, line, lineno);
    if (ctx->section == ASM_DATA)
        return asm_data_line(ctx, pos, lineno);
    if (pos[0] == ':')
        return asm_label(ctx, pos, lineno);

    /* `name:` outside .data is a misplaced data entry, not an instr. */
    size_t name_len = 0;
    while (asm_is_name_char(pos[name_len]))
        name_len++;

    if (pos[name_len] == ':') {
        LOG_E("line %zu: data entries belong in section .data", lineno);
        return false;
    }

    return asm_text_line(ctx, (char *)pos, lineno);
}

/* Patch one recorded `:name` use with its absolute or PC-relative imm. */
static bool asm_patch_word(AsmCtx *ctx, const AsmFixup *fix, size_t target)
{
    assert(ctx != NULL);
    assert(fix != NULL);

    uint32_t word = wst_vec_u32_get(&ctx->obj->code, fix->off / 4);

    int64_t imm = 0;
    if (wst_vm_format_table[fix->fmt].pc_relative) {
        imm = (int64_t)target - (int64_t)fix->off;
    } else {
        /* Memory immediates count in access-size units. */
        unsigned scale = wst_vm_op_table[(unsigned)(word & 0x7Fu)].imm_scale;
        if (target % scale != 0) {
            LOG_E("line %zu: address of '%s' is not %u-aligned",
                  fix->lineno, fix->name, scale);
            return false;
        }
        target /= scale;
        imm = (int64_t)target;
    }

    if (!asm_check_imm(fix->fmt, imm, fix->lineno))
        return false;

    word = asm_set_imm(word, fix->fmt, imm);

    if (wst_vec_u32_set(&ctx->obj->code, fix->off / 4, word) != WST_VEC_NO_ERR) {
        LOG_E("line %zu: address space damaged", fix->lineno);
        return false;
    }

    return true;
}

/* Entry address from `:_start`, zero when the source has none. */
static bool asm_entry(AsmCtx *ctx)
{
    assert(ctx != NULL);

    AsmSym start;
    if (!asm_sym_find(ctx, "_start", &start))
        return true;

    if (start.type != ASM_SYM_LABEL) {
        LOG_E("entry `_start` must be a code label");
        return false;
    }

    ctx->obj->entry = start.addr;
    return true;
}

/* Second pass, every `:name` becomes an absolute or PC relative imm. */
static bool asm_resolve(AsmCtx *ctx)
{
    assert(ctx != NULL);

    size_t data_base = wst_prog_data_addr(asm_code_bytes(ctx));

    for (size_t i = 0; i < ctx->fixups.length; i++) {
        AsmFixup fix;
        wst_vec_fixup_get(&ctx->fixups, i, &fix);
        AsmSym sym;
        if (!asm_sym_find(ctx, fix.name, &sym)) {
            LOG_E("line %zu: unknown symbol '%s'", fix.lineno, fix.name);
            return false;
        }

        size_t target = sym.addr;
        if (sym.type == ASM_SYM_DATA) {
            if (target > SIZE_MAX - data_base) {
                LOG_E("line %zu: symbol '%s' address overflows",
                      fix.lineno, fix.name);
                return false;
            }
            target = data_base + target;
        }

        if (!asm_patch_word(ctx, &fix, target))
            return false;
    }

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

    bool ok = asm_parse_instr(tmp, lineno, false, &word, &sym, &fmt);
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

    wst_vec_u32_free(&out->code);
    wst_vec_u8_free(&out->data);
    out->entry = 0;

    char *copy = calloc(1, len + 1);
    if (copy == NULL) {
        LOG_E("out of memory");
        return false;
    }
    memcpy(copy, src, len);
    copy[len] = '\0';

    AsmCtx ctx = {
        .obj = out,
        .section = ASM_TEXT,
    };

    bool ok = true;
    size_t lineno = 0;
    char *pos = copy;
    while (*pos != '\0') {
        char *line_end = strchr(pos, '\n');

        size_t line_len = line_end != NULL ? (size_t)(line_end - pos) : strlen(pos);
        if (line_len > 0 && pos[line_len - 1] == '\r')
            line_len--;

        pos[line_len] = '\0';
        lineno++;

        asm_strip_comment(pos);
        if (!asm_source_line(&ctx, pos, lineno)) {
            ok = false;
            break;
        }
        pos += line_len + (line_end != NULL ? 1 : 0);
    }

    if (ok)
        ok = asm_resolve(&ctx);
    if (ok)
        ok = asm_entry(&ctx);

    asm_tables_free(&ctx);
    free(copy);

    if (!ok) {
        wst_vec_u32_free(&out->code);
        wst_vec_u8_free(&out->data);
    }
    return ok;
}
