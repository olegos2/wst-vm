#include "toys/disasm.h"
#include "toys/debug.h"
#include "toys/vm.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>


/* Register name into `buf`, `xN` or `fN` by the operand slot. */
static bool dis_reg(unsigned operands, unsigned slot, unsigned reg,
                    char *buf, size_t cap, size_t *len_inout)
{
    assert(buf != NULL);
    assert(len_inout != NULL);

    if (reg > 7)
        return false;

    char rname[4] = {
        (char)(((operands & slot) != 0) ? 'f' : 'x'),
        (char)('0' + reg),
        '\0',
    };

    int n = snprintf(buf + *len_inout, cap - *len_inout, "%s", rname);
    if (n < 0 || (size_t)n >= cap - *len_inout)
        return false;

    *len_inout += (size_t)n;
    return true;
}

static bool dis_put(const char *text, char *buf, size_t cap, size_t *len_inout)
{
    assert(text != NULL);
    assert(buf != NULL);
    assert(len_inout != NULL);

    int n = snprintf(buf + *len_inout, cap - *len_inout, "%s", text);
    if (n < 0 || (size_t)n >= cap - *len_inout)
        return false;

    *len_inout += (size_t)n;
    return true;
}

static bool dis_imm(int64_t imm, char *buf, size_t cap, size_t *len_inout)
{
    assert(buf != NULL);
    assert(len_inout != NULL);

    int n = snprintf(buf + *len_inout, cap - *len_inout, "%" PRId64 , imm);
    if (n < 0 || (size_t)n >= cap - *len_inout)
        return false;

    *len_inout += (size_t)n;
    return true;
}

/* Same math as the interpreter sign extension, kept local. */
static int64_t dis_sign_ext(uint32_t field, unsigned bits)
{
    assert(bits > 0 && bits <= 32);

    uint64_t clean = field & (((uint64_t)1 << bits) - 1);
    uint64_t sign = (uint64_t)1 << (bits - 1);

    return (int64_t)(clean ^ sign) - (int64_t)sign;
}

bool wst_vm_disasm_word(uint32_t word, char *buf, size_t cap)
{
    assert(buf != NULL);

    if (cap == 0)
        return false;
    buf[0] = '\0';

    unsigned op = word & 0x7Fu;
    if (op >= WST_VM_OPCODE_COUNT) {
        LOG_E("unknown opcode 0x%02x", op);
        return false;
    }

    const WstVmInstrDesc *desc = &wst_vm_op_table[op];
    if (desc->format == WST_VM_INSTR_FORMAT_INVALID || desc->mnemonic == NULL) {
        LOG_E("unknown opcode 0x%02x", op);
        return false;
    }

    unsigned rd = 0, rs1 = 0, rs2 = 0;
    int64_t imm = 0;
    switch (desc->format) {
    case WST_VM_INSTR_FORMAT_R:
        rd = (word >> 7) & 0x7u;
        rs1 = (word >> 10) & 0x7u;
        rs2 = (word >> 13) & 0x7u;
        break;
    case WST_VM_INSTR_FORMAT_I:
        rd = (word >> 7) & 0x7u;
        rs1 = (word >> 10) & 0x7u;
        imm = dis_sign_ext(word >> 13, 19);
        break;
    case WST_VM_INSTR_FORMAT_S:
    case WST_VM_INSTR_FORMAT_B:
        rs1 = (word >> 7) & 0x7u;
        rs2 = (word >> 10) & 0x7u;
        imm = dis_sign_ext(word >> 13, 19);
        break;
    case WST_VM_INSTR_FORMAT_U:
        rd = (word >> 7) & 0x7u;
        imm = (int64_t)(((uint64_t)(word >> 12) & 0xFFFFFu) << 12);
        break;
    case WST_VM_INSTR_FORMAT_J:
        rd = (word >> 7) & 0x7u;
        imm = dis_sign_ext(word >> 10, 22);
        break;
    default:
        return false;
    }

    size_t len = 0;
    if (!dis_put(desc->mnemonic, buf, cap, &len))
        return false;

    bool first = true;

    for (unsigned slot = WST_VM_OPND_RD; slot <= WST_VM_OPND_IMM; slot <<= 1) {
        if ((desc->operands & slot) == 0)
            continue;
        if (!dis_put(first ? " " : ", ", buf, cap, &len))
            return false;
        first = false;
        if (slot == WST_VM_OPND_IMM) {
            if (!dis_imm(imm, buf, cap, &len))
                return false;
        } else {
            unsigned reg = slot == WST_VM_OPND_RD ? rd :
                           slot == WST_VM_OPND_RS1 ? rs1 : rs2;
            if (!dis_reg(desc->fregs, slot, reg, buf, cap, &len))
                return false;
        }
    }

    return true;
}
