#include "toys/vm.h"
#include "vm_priv.h"
#include "toys/debug.h"
#include "toys/prog.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int64_t vm_sign_ext(uint32_t field, unsigned width)
{
    assert(width > 0 && width <= 32);

    uint64_t clean = field & (((uint64_t)1 << width) - 1);
    uint64_t sign = (uint64_t)1 << (width - 1);

    return (int64_t)(clean ^ sign) - (int64_t)sign;
}

/* Zero means default, set `mem_max` before load to override. */
static size_t vm_mem_limit(const WstVmContext *ctx)
{
    assert(ctx != NULL);

    return ctx->mem_max != 0 ? ctx->mem_max : WST_VM_MEM_DEFAULT_MAX;
}

/* Zero means default, set `stack_size` before load to override. */
static size_t vm_stack_size(const WstVmContext *ctx)
{
    assert(ctx != NULL);

    return ctx->stack.size != 0 ? ctx->stack.size : WST_VM_STACK_DEFAULT_SIZE;
}

static void vm_mem_ensure(WstVmContext *ctx, uint64_t addr, uint64_t size)
{
    if (size > ctx->mem_size || addr > (uint64_t)ctx->mem_size - size) {
        LOG_E("address 0x%08" PRIx64 " past mem end 0x%08zx",
              addr, ctx->mem_size);
        exit(EXIT_FAILURE);
    }
}

/* Size-specific accessors, misaligned access is fatal. */
#define VM_DEF_MEM_RW(bits) \
    U(bits) vm_mem_read##bits(WstVmContext *ctx, uint64_t addr) \
    { \
        vm_mem_ensure(ctx, addr, (bits) / 8); \
        if (addr % ((bits) / 8) != 0) { \
            LOG_E("misaligned " #bits "-bit access at 0x%08" PRIx64, addr); \
            exit(EXIT_FAILURE); \
        } \
        return *(U(bits) *)&ctx->mem[(size_t)addr]; \
    } \
    \
    void vm_mem_write##bits(WstVmContext *ctx, uint64_t addr, U(bits) value) \
    { \
        vm_mem_ensure(ctx, addr, (bits) / 8); \
        if (addr % ((bits) / 8) != 0) { \
            LOG_E("misaligned " #bits "-bit access at 0x%08" PRIx64, addr); \
            exit(EXIT_FAILURE); \
        } \
        *(U(bits) *)&ctx->mem[(size_t)addr] = value; \
    }

VM_DEF_MEM_RW(64)
VM_DEF_MEM_RW(32)

void vm_stack_push64(WstVm *vm, WstVmContext *ctx, size_t pc, uint64_t value)
{
    uint64_t sp = R_SP;

    if (sp % 8 != 0) {
        LOG_E("misaligned sp 0x%08" PRIx64 " at pc 0x%08zx", sp, pc);
        exit(EXIT_FAILURE);
    }
    if (sp < (uint64_t)ctx->stack.base + 8 || sp > (uint64_t)ctx->stack.top) {
        LOG_E("stack overflow sp 0x%08" PRIx64 " at pc 0x%08zx", sp, pc);
        exit(EXIT_FAILURE);
    }

    R_SP = sp - 8;
    vm_mem_write64(ctx, sp - 8, value);
}

uint64_t vm_stack_pop64(WstVm *vm, WstVmContext *ctx, size_t pc)
{
    uint64_t sp = R_SP;

    if (sp % 8 != 0) {
        LOG_E("misaligned sp 0x%08" PRIx64 " at pc 0x%08zx", sp, pc);
        exit(EXIT_FAILURE);
    }
    if (sp >= (uint64_t)ctx->stack.top || sp < (uint64_t)ctx->stack.base) {
        LOG_E("stack underflow sp 0x%08" PRIx64 " at pc 0x%08zx", sp, pc);
        exit(EXIT_FAILURE);
    }

    R_SP = sp + 8;
    return vm_mem_read64(ctx, sp);
}

bool wst_vm_init(WstVm *vm)
{
    assert(vm != NULL);

    for (size_t i = 0; i < WST_VM_NUM_GPR; i++)
        vm->regs[i] = 0;
    for (size_t i = 0; i < WST_VM_NUM_FP; i++)
        vm->fp[i] = 0.0;
    vm->pc = 0;
    return true;
}

bool wst_vm_context_load(WstVmContext *ctx, const char *path)
{
    assert(ctx != NULL);
    assert(path != NULL);

    free(ctx->mem);
    ctx->mem = NULL;
    ctx->mem_size = 0;
    ctx->entry = 0;
    ctx->stack.base = 0;
    ctx->stack.top = 0;

    size_t limit = vm_mem_limit(ctx);
    size_t top = limit & ~(size_t)7;
    size_t stack_size = vm_stack_size(ctx);
    if (stack_size > top) {
        LOG_E("stack of 0x%08zx bytes past mem end 0x%08zx", stack_size, top);
        return false;
    }
    stack_size = (stack_size + 7) & ~(size_t)7;

    uint8_t *mem = calloc(limit, sizeof(uint8_t));
    if (mem == NULL) {
        LOG_E("cannot reserve 0x%08zx bytes", limit);
        return false;
    }

    /* Direct dereferences need an 8-aligned base. */
    assert(((uintptr_t)mem & 7u) == 0);

    size_t entry = 0;
    size_t base = top - stack_size;
    if (!wst_prog_load(mem, base, &entry, path)) {
        free(mem);
        return false;
    }

    ctx->mem = mem;
    ctx->mem_size = limit;
    ctx->entry = entry;
    ctx->stack.base = base;
    ctx->stack.top = top;
    return true;
}

void wst_vm_context_free(WstVmContext *ctx)
{
    if (ctx == NULL)
        return;

    free(ctx->mem);
    ctx->mem = NULL;
    ctx->mem_size = 0;
    ctx->entry = 0;
    ctx->stack.base = 0;
    ctx->stack.top = 0;
}

bool wst_vm_decode_word(uint32_t word, size_t addr, WstVmInstr *ins)
{
    assert(ins != NULL);

    WstVmOp op = (WstVmOp)(word & 0x7Fu);
    const WstVmInstrDesc *desc = &wst_vm_op_table[(unsigned)op];
    if (desc->format == WST_VM_INSTR_FORMAT_INVALID) {
        LOG_E("unknown opcode 0x%02x at 0x%08zx", (unsigned)op, addr);
        return false;
    }

    ins->op = op;
    ins->rd = 0;
    ins->rs1 = 0;
    ins->rs2 = 0;
    ins->imm = 0;
    ins->addr = addr;

    const WstVmInstrFormatDesc *fmt = &wst_vm_format_table[desc->format];

    if (fmt->rd_shift != 0)
        ins->rd = (uint8_t)((word >> fmt->rd_shift) & 0x7u);
    if (fmt->rs1_shift != 0)
        ins->rs1 = (uint8_t)((word >> fmt->rs1_shift) & 0x7u);
    if (fmt->rs2_shift != 0)
        ins->rs2 = (uint8_t)((word >> fmt->rs2_shift) & 0x7u);

    if (fmt->imm_shift != 0) {
        uint32_t field = word >> fmt->imm_shift;

        if (fmt->imm_signed) {
            ins->imm = vm_sign_ext(field, fmt->imm_bits);
        } else {
            uint32_t mask = (1u << fmt->imm_bits) - 1u;
            ins->imm = (int64_t)(field & mask) << fmt->imm_shift;
        }
    }

    return true;
}

/* Append text to the output buffer. */
static bool vm_text_put(const char *text, char *buf, size_t cap,
                        size_t *len_inout)
{
    int n = snprintf(buf + *len_inout, cap - *len_inout, "%s", text);
    if (n < 0 || (size_t)n >= cap - *len_inout)
        return false;

    *len_inout += (size_t)n;
    return true;
}

bool wst_vm_instr_text(const WstVmInstr *ins, const char *imm_text,
                       char *buf, size_t cap)
{
    assert(ins != NULL);
    assert(buf != NULL);

    if (cap == 0)
        return false;
    buf[0] = '\0';

    const WstVmInstrDesc *desc = &wst_vm_op_table[(unsigned)ins->op];
    if (desc->mnemonic == NULL)
        return false;

    size_t len = 0;
    if (!vm_text_put(desc->mnemonic, buf, cap, &len))
        return false;

    bool first = true;

    for (unsigned slot = WST_VM_OPND_RD; slot <= WST_VM_OPND_IMM; slot <<= 1) {
        if ((desc->operands & slot) == 0)
            continue;

        if (!vm_text_put(first ? " " : ", ", buf, cap, &len))
            return false;
        first = false;

        if (slot == WST_VM_OPND_IMM) {
            if (imm_text != NULL) {
                if (!vm_text_put(imm_text, buf, cap, &len))
                    return false;
                continue;
            }

            int n = snprintf(buf + len, cap - len, "%" PRId64, ins->imm);
            if (n < 0 || (size_t)n >= cap - len)
                return false;
            len += (size_t)n;
            continue;
        }

        unsigned reg = slot == WST_VM_OPND_RD ? ins->rd :
                       slot == WST_VM_OPND_RS1 ? ins->rs1 : ins->rs2;
        char rname[4] = { (char)(((desc->fregs & slot) != 0) ? 'f' : 'x'),
                          (char)('0' + reg), '\0' };
        if (!vm_text_put(rname, buf, cap, &len))
            return false;
    }

    return true;
}

bool wst_vm_decode(const WstVm *vm, const WstVmContext *ctx, WstVmInstr *ins)
{
    assert(vm != NULL);
    assert(ctx != NULL);
    assert(ins != NULL);

    if (vm->pc % WST_VM_INSTR_SIZE != 0) {
        LOG_E("misaligned pc 0x%08zx", vm->pc);
        return false;
    }

    if (vm->pc >= ctx->mem_size ||
        ctx->mem_size - vm->pc < WST_VM_INSTR_SIZE) {
        LOG_E("pc 0x%08zx past mem end 0x%08zx", vm->pc, ctx->mem_size);
        return false;
    }

    /* Fetch never grows memory, past-end is a decode error (checked above).
     * `pc` is 4-aligned here, so this is an aligned dereference. */
    uint32_t word = *(const uint32_t *)&ctx->mem[vm->pc];

    if (!wst_vm_decode_word(word, vm->pc, ins))
        return false;

#ifdef WST_DEBUG
    char text[WST_VM_INSTR_TEXT_CAP] = { 0 };
    if (wst_vm_instr_text(ins, NULL, text, sizeof(text)))
        LOG_V("0x%08zx  %s", vm->pc, text);
#endif

    return true;
}

void wst_vm_execute(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    assert(vm != NULL);
    assert(ctx != NULL);
    assert(ins != NULL);

    /* Decode only fills valid ops, so handler always exists. */
    WstVmInstrHandler handler = wst_vm_op_table[(unsigned)ins->op].handler;
    assert(handler != NULL);

    vm->pc = ins->addr + WST_VM_INSTR_SIZE;
    handler(vm, ctx, ins);
}

void wst_vm_close(WstVm *vm)
{
    if (vm == NULL)
        return;

    vm->pc = 0;
}
