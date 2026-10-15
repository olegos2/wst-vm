#include "asm_priv.h"
#include "toys/debug.h"
#include "toys/vm.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
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


static const WstVmInstrDesc *asm_find_op_n(const char *name, size_t n)
{
    assert(name != NULL);

    for (unsigned i = 0; i < WST_VM_OPCODE_COUNT; i++) {
        const WstVmInstrDesc *desc = &wst_vm_op_table[i];
        if (desc->format != WST_VM_INSTR_FORMAT_INVALID &&
            desc->mnemonic != NULL &&
            strlen(desc->mnemonic) == n &&
            memcmp(desc->mnemonic, name, n) == 0)
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

bool asm_check_imm(WstVmInstrFormat fmt, int64_t imm, size_t lineno)
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
        LOG_E("line %zu: immediate %" PRId64 " out of range", lineno, imm);
    return ok;
}

uint32_t asm_set_imm19(uint32_t word, int64_t imm)
{
    return (word & 0x1FFFu) | ((uint32_t)((uint64_t)imm & 0x7FFFFu) << 13);
}

uint32_t asm_set_imm22(uint32_t word, int64_t imm)
{
    return (word & 0x3FFu) | ((uint32_t)((uint64_t)imm & 0x3FFFFFu) << 10);
}

uint32_t asm_set_imm20(uint32_t word, int64_t imm)
{
    uint64_t hi = ((uint64_t)imm >> 12) & 0xFFFFFu;
    return (word & 0xFFFu) | ((uint32_t)hi << 12);
}

/* `xN` or `fN` with `N` in 0..7, file picked by the operand slot. */
static bool asm_parse_reg(const char *tok, bool is_float, unsigned *reg_out)
{
    assert(tok != NULL);
    assert(reg_out != NULL);

    if (strlen(tok) != 2)
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

    size_t alen = strlen(args);
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
        char *t = toks[n - 1] + strlen(toks[n - 1]);
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

bool asm_parse_instr(char *line, size_t lineno, bool allow_sym,
                     uint32_t *word_out, char **sym_out,
                     WstVmInstrFormat *fmt_out)
{
    assert(line != NULL);
    assert(word_out != NULL);
    assert(sym_out != NULL);
    assert(fmt_out != NULL);

    *sym_out = NULL;

    const char *mp = asm_skip_ws(line);
    size_t mlen = strcspn(mp, " \t\r");
    const WstVmInstrDesc *desc = asm_find_op_n(mp, mlen);
    if (desc == NULL) {
        LOG_E("line %zu: unknown mnemonic", lineno);
        return false;
    }

    char *toks[3] = { NULL, NULL, NULL };
    size_t ntoks = 0;
    if (!asm_split_ops((char *)mp + mlen, toks, &ntoks, lineno))
        return false;

    /* Operand bits ascend in assembly order: RD, RS1, RS2, IMM. */
    size_t need = 0;
    for (unsigned m = desc->operands; m != 0; m >>= 1)
        need += m & 1u;
    if (ntoks != need) {
        LOG_E("line %zu: '%s' needs %zu operands, got %zu",
              lineno, desc->mnemonic, need, ntoks);
        return false;
    }

    unsigned rd = 0, rs1 = 0, rs2 = 0;
    int64_t imm = 0;
    size_t ti = 0;
    for (unsigned slot = WST_VM_OPND_RD; slot <= WST_VM_OPND_IMM; slot <<= 1) {
        if ((desc->operands & slot) == 0)
            continue;

        const char *tok = toks[ti++];
        
        if (slot != WST_VM_OPND_IMM) {
            bool is_float = (desc->fregs & slot) != 0;
            unsigned *dst = slot == WST_VM_OPND_RD ? &rd :
                            slot == WST_VM_OPND_RS1 ? &rs1 : &rs2;
            if (!asm_parse_reg(tok, is_float, dst)) {
                LOG_E("line %zu: bad %s register '%s'",
                      lineno, is_float ? "float" : "integer", tok);
                return false;
            }
            continue;
        }

        if (tok[0] != ':') {
            if (!asm_parse_num(tok, &imm)) {
                LOG_E("line %zu: bad number '%s'", lineno, tok);
                return false;
            }
            if (!asm_check_imm(desc->format, imm, lineno))
                return false;

            continue;
        }

        if (!allow_sym) {
            LOG_E("line %zu: label '%s' needs whole-source assembly",
                    lineno, tok);
            return false;
        }
        if (!asm_valid_name(tok + 1)) {
            LOG_E("line %zu: bad symbol name '%s'", lineno, tok);
            return false;
        }
        *sym_out = strdup(tok + 1);
        if (*sym_out == NULL) {
            LOG_E("line %zu: out of memory", lineno);
            return false;
        }
        imm = 0;
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

bool asm_emit_word(AsmCtx *ctx, uint32_t word, size_t lineno)
{
    assert(ctx != NULL);

    for (size_t i = 0; i < 4; i++) {
        if (wst_vec_u8_push(&ctx->obj->code, (uint8_t)((word >> (8 * i)) & 0xFFu)) != WST_VEC_NO_ERR) {
            LOG_E("line %zu: out of memory", lineno);
            return false;
        }
    }
    return true;
}
