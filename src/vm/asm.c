#include "toys/asm.h"
#include "toys/debug.h"
#include "toys/prog.h"
#include "toys/string.h"
#include "toys/vec_internal.h"
#include "toys/vm.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/* Signed 19-bit and 22-bit immediate limits. */
#define ASM_S19_MIN (-(INT64_C(1) << 18))
#define ASM_S19_MAX ((INT64_C(1) << 18) - 1)
#define ASM_S22_MIN (-(INT64_C(1) << 21))
#define ASM_S22_MAX ((INT64_C(1) << 21) - 1)

/* Largest encodable `lui` constant, high 20 bits shifted by 12. */
#define ASM_U_MAX ((int64_t)0xfffff000)


/** Label or data name with its code address or data offset. */
typedef struct {
    char *name;
    size_t addr;
    bool is_data;
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


void wst_vm_asm_object_free(WstAsmObject *obj)
{
    if (obj == NULL)
        return;

    WstVecErr e1 = wst_vec_u8_free(&obj->code);
    WstVecErr e2 = wst_vec_u8_free(&obj->data);
    if (e1 != WST_VEC_NO_ERR || e2 != WST_VEC_NO_ERR)
        LOG_W("object damaged: %s / %s", wst_vec_err_str(e1), wst_vec_err_str(e2));
}

static bool asm_is_name_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool asm_is_name_char(char c)
{
    return asm_is_name_start(c) || (c >= '0' && c <= '9');
}

static bool asm_is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static const char *asm_skip_ws(const char *p)
{
    while (asm_is_ws(*p))
        p++;
    return p;
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

static bool asm_valid_name(const char *name)
{
    if (!asm_is_name_start(name[0]))
        return false;
    for (const char *p = name + 1; *p != '\0'; p++) {
        if (!asm_is_name_char(*p))
            return false;
    }
    return true;
}

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

static const AsmSym *asm_sym_find(const AsmCtx *ctx, const char *name)
{
    assert(ctx != NULL);
    assert(name != NULL);

    for (size_t i = 0; i < ctx->syms.len; i++) {
        if (wst_strcmp(ctx->syms.items[i].name, name) == 0)
            return &ctx->syms.items[i];
    }
    return NULL;
}

static bool asm_sym_add(AsmCtx *ctx, const char *name, size_t addr,
                        bool is_data, size_t lineno)
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

    char *dup = wst_strdup(name);
    if (dup == NULL) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }

    ctx->syms.items[ctx->syms.len].name = dup;
    ctx->syms.items[ctx->syms.len].addr = addr;
    ctx->syms.items[ctx->syms.len].is_data = is_data;
    ctx->syms.len++;
    return true;
}

static bool asm_fix_add(AsmCtx *ctx, size_t off, WstVmInstrFormat fmt,
                        const char *name, size_t lineno)
{
    assert(ctx != NULL);
    assert(name != NULL);

    if (!asm_grow((void **)&ctx->fixups.items, &ctx->fixups.cap,
                  ctx->fixups.len, sizeof(*ctx->fixups.items)))
        return false;

    char *dup = wst_strdup(name);
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

static void asm_tables_free(AsmCtx *ctx)
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

static const WstVmInstrDesc *asm_find_op(const char *name)
{
    assert(name != NULL);

    for (unsigned i = 0; i < WST_VM_OPCODE_COUNT; i++) {
        const WstVmInstrDesc *desc = &wst_vm_op_table[i];
        if (desc->format != WST_VM_INSTR_FORMAT_INVALID &&
            desc->mnemonic != NULL &&
            wst_strcmp(desc->mnemonic, name) == 0)
            return desc;
    }
    return NULL;
}

/* Opcode byte back from a table row pointer. */
static unsigned asm_opcode(const WstVmInstrDesc *desc)
{
    assert(desc != NULL);
    assert(desc >= wst_vm_op_table);
    assert(desc < wst_vm_op_table + WST_VM_OPCODE_COUNT);

    return (unsigned)(desc - wst_vm_op_table);
}

static bool asm_check_imm(WstVmInstrFormat fmt, int64_t imm, size_t lineno)
{
    bool ok = false;
    switch (fmt) {
    case WST_VM_INSTR_FORMAT_I:
    case WST_VM_INSTR_FORMAT_S:
    case WST_VM_INSTR_FORMAT_B:
        ok = imm >= ASM_S19_MIN && imm <= ASM_S19_MAX;
        break;
    case WST_VM_INSTR_FORMAT_J:
        ok = imm >= ASM_S22_MIN && imm <= ASM_S22_MAX;
        break;
    case WST_VM_INSTR_FORMAT_U:
        ok = imm >= 0 && imm <= ASM_U_MAX && (imm & 0xFFF) == 0;
        break;
    default:
        ok = false;
        break;
    }

    if (!ok)
        LOG_E("line %zu: immediate %lld out of range", lineno, (long long)imm);
    return ok;
}

static uint32_t asm_set_imm19(uint32_t word, int64_t imm)
{
    return (word & 0x1FFFu) | ((uint32_t)((uint64_t)imm & 0x7FFFFu) << 13);
}

static uint32_t asm_set_imm22(uint32_t word, int64_t imm)
{
    return (word & 0x3FFu) | ((uint32_t)((uint64_t)imm & 0x3FFFFFu) << 10);
}

static uint32_t asm_set_imm20(uint32_t word, int64_t imm)
{
    uint64_t hi = ((uint64_t)imm >> 12) & 0xFFFFFu;
    return (word & 0xFFFu) | ((uint32_t)hi << 12);
}

/* `xN` or `fN` with `N` in 0..7, file picked by the operand slot. */
static bool asm_parse_reg(const char *tok, bool is_float, unsigned *reg_out)
{
    assert(tok != NULL);
    assert(reg_out != NULL);

    if (wst_strlen(tok) != 2)
        return false;
    if (tok[0] != (is_float ? 'f' : 'x'))
        return false;
    if (tok[1] < '0' || tok[1] > '7')
        return false;

    *reg_out = (unsigned)(tok[1] - '0');
    return true;
}

/* Decimal or hex number with no trailing garbage. */
static bool asm_parse_num(const char *tok, int64_t *num_out)
{
    assert(tok != NULL);
    assert(num_out != NULL);

    char *end = NULL;
    errno = 0;
    long long val = strtoll(tok, &end, 0);
    if (errno != 0 || end == tok || *end != '\0')
        return false;

    *num_out = (int64_t)val;
    return true;
}

/* Split `args` by commas in place, trimming spaces, at most 3 operands. */
static bool asm_split_ops(char *args, char *toks[3], size_t *ntoks_out,
                          size_t lineno)
{
    assert(args != NULL);
    assert(toks != NULL);
    assert(ntoks_out != NULL);

    size_t alen = wst_strlen(args);
    while (alen > 0 && asm_is_ws(args[alen - 1]))
        alen--;
    args[alen] = '\0';
    if (alen > 0 && args[alen - 1] == ',') {
        LOG_E("line %zu: trailing comma", lineno);
        return false;
    }

    size_t n = 0;
    char *p = args;
    while (1) {
        p = (char *)asm_skip_ws(p);
        if (*p == '\0')
            break;
        if (n == 3) {
            LOG_E("line %zu: too many operands", lineno);
            return false;
        }
        toks[n++] = p;
        while (*p != '\0' && *p != ',')
            p++;
        if (*p == ',') {
            *p = '\0';
            p++;
        }
        char *t = toks[n - 1] + wst_strlen(toks[n - 1]);
        while (t > toks[n - 1] && asm_is_ws(t[-1]))
            *--t = '\0';
        if (toks[n - 1][0] == '\0') {
            LOG_E("line %zu: empty operand", lineno);
            return false;
        }
    }

    *ntoks_out = n;
    return true;
}

/* Slot order is fixed per format, gaps for slots outside the mask. */
static bool asm_slot_order(WstVmInstrFormat fmt, unsigned slots[4], size_t *n_out)
{
    assert(slots != NULL);
    assert(n_out != NULL);

    switch (fmt) {
    case WST_VM_INSTR_FORMAT_R:
        slots[0] = WST_VM_OPND_RD;
        slots[1] = WST_VM_OPND_RS1;
        slots[2] = WST_VM_OPND_RS2;
        *n_out = 3;
        return true;
    case WST_VM_INSTR_FORMAT_I:
        slots[0] = WST_VM_OPND_RD;
        slots[1] = WST_VM_OPND_RS1;
        slots[2] = WST_VM_OPND_IMM;
        *n_out = 3;
        return true;
    case WST_VM_INSTR_FORMAT_S:
    case WST_VM_INSTR_FORMAT_B:
        slots[0] = WST_VM_OPND_RS1;
        slots[1] = WST_VM_OPND_RS2;
        slots[2] = WST_VM_OPND_IMM;
        *n_out = 3;
        return true;
    case WST_VM_INSTR_FORMAT_U:
    case WST_VM_INSTR_FORMAT_J:
        slots[0] = WST_VM_OPND_RD;
        slots[1] = WST_VM_OPND_IMM;
        *n_out = 2;
        return true;
    default:
        return false;
    }
}

/*
 * Encode one instruction, `line` holds `mnemonic operands...` already
 * stripped of comments. Numeric operands encode inline, a `:name`
 * immediate needs `allow_sym` and comes back in `sym_out`.
 */
static bool asm_parse_insn(char *line, size_t lineno, bool allow_sym,
                           uint32_t *word_out, char **sym_out,
                           WstVmInstrFormat *fmt_out)
{
    assert(line != NULL);
    assert(word_out != NULL);
    assert(sym_out != NULL);
    assert(fmt_out != NULL);

    *sym_out = NULL;

    const char *mp = asm_skip_ws(line);
    size_t mlen = 0;
    while (mp[mlen] != '\0' && !asm_is_ws(mp[mlen]))
        mlen++;

    char saved = ((char *)mp)[mlen];
    ((char *)mp)[mlen] = '\0';
    const WstVmInstrDesc *desc = asm_find_op(mp);
    ((char *)mp)[mlen] = saved;
    if (desc == NULL) {
        LOG_E("line %zu: unknown mnemonic", lineno);
        return false;
    }

    char *toks[3] = { NULL, NULL, NULL };
    size_t ntoks = 0;
    if (!asm_split_ops((char *)mp + mlen, toks, &ntoks, lineno))
        return false;

    unsigned slots[4] = { 0, 0, 0, 0 };
    size_t nslots = 0;
    if (!asm_slot_order(desc->format, slots, &nslots))
        return false;

    size_t need = 0;
    for (size_t i = 0; i < nslots; i++) {
        if ((desc->operands & slots[i]) != 0)
            need++;
    }
    if (ntoks != need) {
        LOG_E("line %zu: '%s' needs %zu operands, got %zu",
              lineno, desc->mnemonic, need, ntoks);
        return false;
    }

    unsigned rd = 0, rs1 = 0, rs2 = 0;
    int64_t imm = 0;
    size_t ti = 0;
    for (size_t i = 0; i < nslots; i++) {
        if ((desc->operands & slots[i]) == 0)
            continue;

        const char *tok = toks[ti++];
        if (slots[i] == WST_VM_OPND_IMM) {
            if (tok[0] == ':') {
                if (!allow_sym) {
                    LOG_E("line %zu: label '%s' needs whole-source assembly",
                          lineno, tok);
                    return false;
                }
                if (!asm_valid_name(tok + 1)) {
                    LOG_E("line %zu: bad symbol name '%s'", lineno, tok);
                    return false;
                }
                *sym_out = wst_strdup(tok + 1);
                if (*sym_out == NULL) {
                    LOG_E("line %zu: out of memory", lineno);
                    return false;
                }
                imm = 0;
            } else {
                if (!asm_parse_num(tok, &imm)) {
                    LOG_E("line %zu: bad number '%s'", lineno, tok);
                    return false;
                }
                if (!asm_check_imm(desc->format, imm, lineno))
                    return false;
            }
        } else {
            bool is_float = (desc->fregs & slots[i]) != 0;
            unsigned *dst = slots[i] == WST_VM_OPND_RD ? &rd :
                            slots[i] == WST_VM_OPND_RS1 ? &rs1 : &rs2;
            if (!asm_parse_reg(tok, is_float, dst)) {
                LOG_E("line %zu: bad %s register '%s'",
                      lineno, is_float ? "float" : "integer", tok);
                return false;
            }
        }
    }

    uint32_t word = (uint32_t)asm_opcode(desc);
    switch (desc->format) {
    case WST_VM_INSTR_FORMAT_R:
        word |= (uint32_t)(rd << 7) | (uint32_t)(rs1 << 10) | (uint32_t)(rs2 << 13);
        break;
    case WST_VM_INSTR_FORMAT_I:
        word |= (uint32_t)(rd << 7) | (uint32_t)(rs1 << 10);
        word = asm_set_imm19(word, imm);
        break;
    case WST_VM_INSTR_FORMAT_S:
    case WST_VM_INSTR_FORMAT_B:
        word |= (uint32_t)(rs1 << 7) | (uint32_t)(rs2 << 10);
        word = asm_set_imm19(word, imm);
        break;
    case WST_VM_INSTR_FORMAT_U:
        word |= (uint32_t)(rd << 7);
        word = asm_set_imm20(word, imm);
        break;
    case WST_VM_INSTR_FORMAT_J:
        word |= (uint32_t)(rd << 7);
        word = asm_set_imm22(word, imm);
        break;
    default:
        LOG_E("line %zu: bad format", lineno);
        free(*sym_out);
        *sym_out = NULL;
        return false;
    }

    *word_out = word;
    *fmt_out = desc->format;
    return true;
}

static bool asm_emit(AsmCtx *ctx, uint32_t word, size_t lineno)
{
    assert(ctx != NULL);

    uint8_t buf[4] = {
        (uint8_t)(word & 0xFFu),
        (uint8_t)((word >> 8) & 0xFFu),
        (uint8_t)((word >> 16) & 0xFFu),
        (uint8_t)((word >> 24) & 0xFFu),
    };
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (wst_vec_u8_push(&ctx->obj->code, buf[i]) != WST_VEC_NO_ERR) {
            LOG_E("line %zu: out of memory", lineno);
            return false;
        }
    }
    return true;
}

static bool asm_data_push(AsmCtx *ctx, uint8_t byte, size_t lineno)
{
    assert(ctx != NULL);

    if (wst_vec_u8_push(&ctx->obj->data, byte) != WST_VEC_NO_ERR) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }
    return true;
}

static bool asm_data_align(AsmCtx *ctx, size_t align, size_t lineno)
{
    assert(ctx != NULL);

    while (ctx->obj->data.length % align != 0) {
        if (!asm_data_push(ctx, 0, lineno))
            return false;
    }
    return true;
}

/* Unsigned number, no trailing garbage, `-` rejected up front. */
static bool asm_data_num(const char *tok, unsigned long long *num_out)
{
    assert(tok != NULL);
    assert(num_out != NULL);

    if (tok[0] == '-')
        return false;

    char *end = NULL;
    errno = 0;
    unsigned long long val = strtoull(tok, &end, 0);
    if (errno != 0 || end == tok)
        return false;
    end = (char *)asm_skip_ws(end);
    if (*end != '\0')
        return false;

    *num_out = val;
    return true;
}

static bool asm_data_double(const char *tok, double *num_out)
{
    assert(tok != NULL);
    assert(num_out != NULL);

    char *end = NULL;
    errno = 0;
    double val = strtod(tok, &end);
    if (errno != 0 || end == tok)
        return false;
    end = (char *)asm_skip_ws(end);
    if (*end != '\0')
        return false;

    *num_out = val;
    return true;
}

/* Quoted string with `\n \\ \" \0 \t` escapes, exact bytes, no NUL added. */
static bool asm_data_str(AsmCtx *ctx, const char *val, size_t lineno)
{
    assert(ctx != NULL);
    assert(val != NULL);

    if (val[0] != '"') {
        LOG_E("line %zu: string needs opening quote", lineno);
        return false;
    }

    size_t i = 1;
    while (val[i] != '"') {
        if (val[i] == '\0') {
            LOG_E("line %zu: string needs closing quote", lineno);
            return false;
        }
        uint8_t byte = (uint8_t)val[i];
        if (val[i] == '\\') {
            i++;
            switch (val[i]) {
            case 'n':
                byte = (uint8_t)'\n';
                break;
            case 't':
                byte = (uint8_t)'\t';
                break;
            case 'r':
                byte = (uint8_t)'\r';
                break;
            case '0':
                byte = 0;
                break;
            case '\\':
                byte = (uint8_t)'\\';
                break;
            case '"':
                byte = (uint8_t)'"';
                break;
            default:
                LOG_E("line %zu: bad escape '\\%c'", lineno, val[i]);
                return false;
            }
        }
        if (!asm_data_push(ctx, byte, lineno))
            return false;
        i++;
    }

    if (*asm_skip_ws(val + i + 1) != '\0') {
        LOG_E("line %zu: trailing text after string", lineno);
        return false;
    }
    return true;
}

/* `name: <type> <value>` in `section .data`, address is the data offset. */
static bool asm_data_entry(AsmCtx *ctx, char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);

    char *colon = wst_strchr(line, ':');
    if (colon == NULL) {
        LOG_E("line %zu: data entry needs 'name: <type> <value>'", lineno);
        return false;
    }
    *colon = '\0';

    char *name = (char *)asm_skip_ws(line);
    char *nend = name + wst_strlen(name);
    while (nend > name && asm_is_ws(nend[-1]))
        *--nend = '\0';
    if (!asm_valid_name(name)) {
        LOG_E("line %zu: bad data name", lineno);
        return false;
    }

    const char *rest = asm_skip_ws(colon + 1);
    size_t tlen = 0;
    while (rest[tlen] != '\0' && !asm_is_ws(rest[tlen]))
        tlen++;

    const char *val = asm_skip_ws(rest + tlen);
    if (tlen == 0 || *val == '\0') {
        LOG_E("line %zu: data entry needs '<type> <value>'", lineno);
        return false;
    }

    size_t off = 0;
    if ((tlen == 3 && memcmp(rest, "u64", 3) == 0) ||
        (tlen == 3 && memcmp(rest, "f64", 3) == 0)) {
        if (!asm_data_align(ctx, 8, lineno))
            return false;
        off = ctx->obj->data.length;
        uint64_t bits = 0;
        if (tlen == 3 && rest[0] == 'u') {
            unsigned long long num = 0;
            if (!asm_data_num(val, &num)) {
                LOG_E("line %zu: bad u64 value", lineno);
                return false;
            }
            bits = (uint64_t)num;
        } else {
            double num = 0;
            if (!asm_data_double(val, &num)) {
                LOG_E("line %zu: bad f64 value", lineno);
                return false;
            }
            memcpy(&bits, &num, sizeof(bits));
        }
        for (unsigned i = 0; i < 8; i++) {
            if (!asm_data_push(ctx, (uint8_t)(bits & 0xFFu), lineno))
                return false;
            bits >>= 8;
        }
    } else if (tlen == 3 && memcmp(rest, "u32", 3) == 0) {
        if (!asm_data_align(ctx, 4, lineno))
            return false;
        off = ctx->obj->data.length;
        unsigned long long num = 0;
        if (!asm_data_num(val, &num) || num > 0xFFFFFFFFu) {
            LOG_E("line %zu: bad u32 value", lineno);
            return false;
        }
        uint64_t bits = (uint64_t)num;
        for (unsigned i = 0; i < 4; i++) {
            if (!asm_data_push(ctx, (uint8_t)(bits & 0xFFu), lineno))
                return false;
            bits >>= 8;
        }
    } else if (tlen == 3 && memcmp(rest, "str", 3) == 0) {
        off = ctx->obj->data.length;
        if (!asm_data_str(ctx, val, lineno))
            return false;
    } else {
        LOG_E("line %zu: unknown data type (u64, u32, f64, str)", lineno);
        return false;
    }

    return asm_sym_add(ctx, name, off, true, lineno);
}

static bool asm_section(AsmCtx *ctx, const char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);

    const char *name = asm_skip_ws(line + wst_strlen("section"));
    if (wst_strcmp(name, ".text") == 0) {
        ctx->in_data = false;
        return true;
    }
    if (wst_strcmp(name, ".data") == 0) {
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
    if (*asm_skip_ws(name + wst_strlen(name)) != '\0') {
        LOG_E("line %zu: label takes no operands", lineno);
        return false;
    }

    return asm_sym_add(ctx, name, ctx->obj->code.length, false, lineno);
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

    if (!asm_emit(ctx, word, lineno)) {
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

    size_t wlen = 0;
    while (p[wlen] != '\0' && !asm_is_ws(p[wlen]))
        wlen++;

    if (wlen == 7 && memcmp(p, "section", 7) == 0)
        return asm_section(ctx, line, lineno);
    if (p[0] == ':')
        return asm_label(ctx, p, lineno);
    if (ctx->in_data)
        return asm_data_entry(ctx, (char *)p, lineno);
    if (wst_strchr(p, ':') != NULL && p[0] != '\0') {
        /* `name:` outside .data is a misplaced data entry, not an insn. */
        const char *q = p;
        while (asm_is_name_char(*q))
            q++;
        if (*q == ':') {
            LOG_E("line %zu: data entries belong in section .data", lineno);
            return false;
        }
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
        if (sym->is_data) {
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

    char *tmp = wst_strdup(line);
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
    char *end = copy + len;
    while (pos < end) {
        char *nl = wst_memchr(pos, '\n', (size_t)(end - pos));
        size_t llen = nl != NULL ? (size_t)(nl - pos) : (size_t)(end - pos);
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
