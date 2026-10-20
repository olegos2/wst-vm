#include "asm_priv.h"
#include "toys/debug.h"
#include "toys/vm.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/* Find an opcode by name of known length. */
static const WstVmInstrDesc *asm_find_op_n(const char *name, size_t n)
{
    assert(name != NULL);

    for (unsigned i = 0; i < WST_VM_NUM_OPCODES; i++) {
        const WstVmInstrDesc *desc = &wst_vm_op_table[i];
        if (desc->format != WST_VM_INSTR_FORMAT_INVALID &&
            desc->mnemonic != NULL &&
            strlen(desc->mnemonic) == n &&
            memcmp(desc->mnemonic, name, n) == 0)
            return desc;
    }
    return NULL;
}

/* Opcode byte of a table row. */
static unsigned asm_opcode(const WstVmInstrDesc *desc)
{
    assert(desc != NULL);
    assert(desc >= wst_vm_op_table);
    assert(desc < wst_vm_op_table + WST_VM_NUM_OPCODES);

    return (unsigned)(desc - wst_vm_op_table);
}

bool asm_check_imm(WstVmInstrFormat fmt, int64_t imm, size_t lineno)
{
    const WstVmInstrFormatDesc *desc = &wst_vm_format_table[fmt];

    int64_t min = 0;
    int64_t max = 0;
    bool ok = desc->imm_shift != 0;

    if (ok && desc->imm_signed) {
        max = (INT64_C(1) << (desc->imm_bits - 1)) - 1;
        min = -max - 1;
    } else if (ok) {
        /* Unsigned immediates scale to the high bits. */
        ok = (imm & ((INT64_C(1) << desc->imm_shift) - 1)) == 0;
        max = ((INT64_C(1) << desc->imm_bits) - 1) << desc->imm_shift;
    }

    if (!ok || imm < min || imm > max) {
        LOG_E("line %zu: immediate %" PRId64 " out of range", lineno, imm);
        return false;
    }

    return true;
}

uint32_t asm_set_imm(uint32_t word, WstVmInstrFormat fmt, int64_t imm)
{
    const WstVmInstrFormatDesc *desc = &wst_vm_format_table[fmt];

    assert(desc->imm_shift != 0);

    /* Unsigned immediates store the value shifted right, signed ones as is. */
    uint64_t field = (uint64_t)(desc->imm_signed
                                ? imm : imm >> desc->imm_shift);
    uint32_t mask = (1u << desc->imm_bits) - 1u;

    return (word & ((1u << desc->imm_shift) - 1u)) |
           ((uint32_t)(field & mask) << desc->imm_shift);
}

/* Parse one register token, `x0`-`x7` or `f0`-`f7`. */
static bool asm_parse_reg(const char *tok, bool is_float, unsigned *reg_out)
{
    assert(tok != NULL);
    assert(reg_out != NULL);

    if (strlen(tok) != 2)
        return false;
    if (tok[0] != (is_float ? 'f' : 'x'))
        return false;
    if (tok[1] < '0' || (unsigned)(tok[1] - '0') >= WST_VM_NUM_GPR)
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

/* Split `args` by commas in place, trimming spaces, one token per slot. */
static bool asm_split_ops(char *args, char *toks[WST_VM_OPND_COUNT],
                          size_t *ntoks_out, size_t lineno)
{
    assert(args != NULL);
    assert(toks != NULL);
    assert(ntoks_out != NULL);

    size_t args_len = strlen(args);
    while (args_len > 0 && asm_is_ws(args[args_len - 1]))
        args_len--;
    args[args_len] = '\0';

    if (args_len > 0 && args[args_len - 1] == ',') {
        LOG_E("line %zu: trailing comma", lineno);
        return false;
    }

    size_t ntoks = 0;
    char *pos = args;
    while (1) {
        pos = (char *)asm_skip_ws(pos);
        if (*pos == '\0')
            break;

        if (ntoks == WST_VM_OPND_COUNT) {
            LOG_E("line %zu: too many operands", lineno);
            return false;
        }

        char *tok = pos;
        ntoks++;
        toks[ntoks - 1] = tok;

        while (*pos != '\0' && *pos != ',')
            pos++;

        if (*pos == ',') {
            *pos = '\0';
            pos++;
        }

        char *end = tok + strlen(tok);
        while (end > tok && asm_is_ws(end[-1]))
            *--end = '\0';

        if (tok[0] == '\0') {
            LOG_E("line %zu: empty operand", lineno);
            return false;
        }
    }

    *ntoks_out = ntoks;
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

    const char *mnemonic = asm_skip_ws(line);
    size_t mnemonic_len = strcspn(mnemonic, " \t\r");

    const WstVmInstrDesc *desc = asm_find_op_n(mnemonic, mnemonic_len);
    if (desc == NULL) {
        LOG_E("line %zu: unknown mnemonic", lineno);
        return false;
    }

    char *toks[WST_VM_OPND_COUNT] = { 0 };
    size_t ntoks = 0;
    if (!asm_split_ops((char *)mnemonic + mnemonic_len, toks, &ntoks, lineno))
        return false;

    /* Operand bits ascend in assembly order: RD, RS1, RS2, IMM. */
    size_t need = 0;
    for (unsigned mask = desc->operands; mask != 0; mask >>= 1)
        need += mask & 1u;

    if (ntoks != need) {
        LOG_E("line %zu: '%s' needs %zu operands, got %zu",
              lineno, desc->mnemonic, need, ntoks);
        return false;
    }

    unsigned rd = 0;
    unsigned rs1 = 0;
    unsigned rs2 = 0;
    int64_t imm = 0;
    size_t tok_idx = 0;

    for (unsigned slot = WST_VM_OPND_RD; slot <= WST_VM_OPND_IMM; slot <<= 1) {
        if ((desc->operands & slot) == 0)
            continue;

        const char *tok = toks[tok_idx++];

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
    const WstVmInstrFormatDesc *fmt = &wst_vm_format_table[desc->format];

    if (fmt->rd_shift != 0)
        word |= (uint32_t)rd << fmt->rd_shift;
    if (fmt->rs1_shift != 0)
        word |= (uint32_t)rs1 << fmt->rs1_shift;
    if (fmt->rs2_shift != 0)
        word |= (uint32_t)rs2 << fmt->rs2_shift;
    if (fmt->imm_shift != 0)
        word = asm_set_imm(word, desc->format, imm);

    *word_out = word;
    *fmt_out = desc->format;
    return true;
}

bool asm_emit_word(AsmCtx *ctx, uint32_t word, size_t lineno)
{
    assert(ctx != NULL);

    if (wst_vec_u32_push(&ctx->obj->code, word) != WST_VEC_NO_ERR) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }
    return true;
}
