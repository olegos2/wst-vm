#include "toys/vm.h"
#include "vm_priv.h"
#include "toys/debug.h"
#include "toys/prog.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
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
    size_t limit = vm_mem_limit(ctx);
    if (size > limit || addr > (uint64_t)limit - size) {
        LOG_E("address %" PRIu64 " past address space end", addr);
        exit(EXIT_FAILURE);
    }

    if (wst_vec_u8_ensure(&ctx->mem, (size_t)(addr + size)) != WST_VEC_NO_ERR) {
        LOG_E("cannot grow address space");
        exit(EXIT_FAILURE);
    }
}

/* Size-specific accessors, sizes are baked in like flux64 `U(sz)` impls.
 * Direct dereferences: stack slots and data words are aligned in
 * practice, same tradeoff as the reference emulator. */
#define VM_DEF_MEM_RW(bits) \
    uint##bits##_t vm_mem_read##bits(WstVmContext *ctx, uint64_t addr) \
    { \
        vm_mem_ensure(ctx, addr, (bits) / 8); \
        return *(uint##bits##_t *)&ctx->mem.data[(size_t)addr]; \
    } \
    \
    void vm_mem_write##bits(WstVmContext *ctx, uint64_t addr, uint##bits##_t value) \
    { \
        vm_mem_ensure(ctx, addr, (bits) / 8); \
        *(uint##bits##_t *)&ctx->mem.data[(size_t)addr] = value; \
        wst_vec_u8_seal(&ctx->mem); \
    }

VM_DEF_MEM_RW(64)
VM_DEF_MEM_RW(32)

void vm_stack_push64(WstVm *vm, WstVmContext *ctx, size_t pc, uint64_t value)
{
    uint64_t sp = R_SP;

    if (sp % 8 != 0) {
        LOG_E("misaligned sp %" PRIu64 " at pc %zu", sp, pc);
        exit(EXIT_FAILURE);
    }
    if (sp < (uint64_t)ctx->stack.base + 8 || sp > (uint64_t)ctx->stack.top) {
        LOG_E("stack overflow at pc %zu", pc);
        exit(EXIT_FAILURE);
    }

    R_SP = sp - 8;
    vm_mem_write64(ctx, sp - 8, value);
}

uint64_t vm_stack_pop64(WstVm *vm, WstVmContext *ctx, size_t pc)
{
    uint64_t sp = R_SP;

    if (sp % 8 != 0) {
        LOG_E("misaligned sp %" PRIu64 " at pc %zu", sp, pc);
        exit(EXIT_FAILURE);
    }
    if (sp >= (uint64_t)ctx->stack.top || sp < (uint64_t)ctx->stack.base) {
        LOG_E("stack underflow at pc %zu", pc);
        exit(EXIT_FAILURE);
    }

    R_SP = sp + 8;
    return vm_mem_read64(ctx, sp);
}

bool wst_vm_init(WstVm *vm)
{
    assert(vm != NULL);

    for (size_t i = 0; i < 8; i++) {
        vm->regs[i] = 0;
        vm->fp[i] = 0.0;
    }
    vm->pc = 0;
    return true;
}

bool wst_vm_context_load(WstVmContext *ctx, const char *path)
{
    assert(ctx != NULL);
    assert(path != NULL);

    wst_vec_u8_free(&ctx->mem);
    ctx->entry = 0;
    ctx->stack.base = 0;
    ctx->stack.top = 0;

    size_t entry = 0;
    if (!wst_prog_load(&ctx->mem, &entry, path, vm_mem_limit(ctx)))
        return false;

    if (ctx->mem.length > SIZE_MAX - 8)
        return false;

    size_t base = (ctx->mem.length + 7) & ~(size_t)7;
    size_t stack_size = vm_stack_size(ctx);
    size_t limit = vm_mem_limit(ctx);
    if (stack_size > limit || base > limit - stack_size) {
        LOG_E("stack of %zu bytes past address space end", stack_size);
        return false;
    }
    if (wst_vec_u8_resize(&ctx->mem, base + stack_size) != WST_VEC_NO_ERR)
        return false;

    ctx->entry = entry;
    ctx->stack.base = base;
    ctx->stack.top = base + stack_size;
    return true;
}

void wst_vm_context_free(WstVmContext *ctx)
{
    if (ctx == NULL)
        return;

    WstVecErr err = wst_vec_u8_free(&ctx->mem);
    if (err != WST_VEC_NO_ERR)
        LOG_W("address space damaged: %s", wst_vec_err_str(err));

    ctx->entry = 0;
    ctx->stack.base = 0;
    ctx->stack.top = 0;
}

bool wst_vm_decode(const WstVm *vm, const WstVmContext *ctx, WstVmInstr *ins)
{
    assert(vm != NULL);
    assert(ctx != NULL);
    assert(ins != NULL);

    if (vm->pc % 4 != 0) {
        LOG_E("misaligned pc %zu", vm->pc);
        return false;
    }

    if (vm->pc >= ctx->mem.length || ctx->mem.length - vm->pc < 4) {
        LOG_E("pc %zu past program end %zu", vm->pc, ctx->mem.length);
        return false;
    }

    /* Fetch never grows memory, past-end is a decode error (checked above).
     * `pc` is 4-aligned here, so this is an aligned dereference. */
    uint32_t word = *(const uint32_t *)&ctx->mem.data[vm->pc];

    WstVmOp op = (WstVmOp)(word & 0x7Fu);
    const WstVmInstrDesc *desc = &wst_vm_op_table[(unsigned)op];
    if (desc->format == WST_VM_INSTR_FORMAT_INVALID) {
        LOG_E("unknown opcode 0x%02x at pc %zu", (unsigned)op, vm->pc);
        return false;
    }

    ins->op = op;
    ins->rd = 0;
    ins->rs1 = 0;
    ins->rs2 = 0;
    ins->imm = 0;
    ins->addr = vm->pc;

    switch (desc->format) {
    case WST_VM_INSTR_FORMAT_R:
        ins->rd = (uint8_t)((word >> 7) & 0x7u);
        ins->rs1 = (uint8_t)((word >> 10) & 0x7u);
        ins->rs2 = (uint8_t)((word >> 13) & 0x7u);
        break;
    case WST_VM_INSTR_FORMAT_I:
        ins->rd = (uint8_t)((word >> 7) & 0x7u);
        ins->rs1 = (uint8_t)((word >> 10) & 0x7u);
        ins->imm = vm_sign_ext(word >> 13, 19);
        break;
    case WST_VM_INSTR_FORMAT_S:
    case WST_VM_INSTR_FORMAT_B:
        ins->rs1 = (uint8_t)((word >> 7) & 0x7u);
        ins->rs2 = (uint8_t)((word >> 10) & 0x7u);
        ins->imm = vm_sign_ext(word >> 13, 19);
        break;
    case WST_VM_INSTR_FORMAT_U:
        ins->rd = (uint8_t)((word >> 7) & 0x7u);
        ins->imm = (int64_t)(((uint64_t)(word >> 12) & 0xFFFFFu) << 12);
        break;
    case WST_VM_INSTR_FORMAT_J:
        ins->rd = (uint8_t)((word >> 7) & 0x7u);
        ins->imm = vm_sign_ext(word >> 10, 22);
        break;
    default:
        break;
    }

    if (desc->format == WST_VM_INSTR_FORMAT_I || desc->format == WST_VM_INSTR_FORMAT_S ||
        desc->format == WST_VM_INSTR_FORMAT_B || desc->format == WST_VM_INSTR_FORMAT_J ||
        desc->format == WST_VM_INSTR_FORMAT_U)
        LOG_V("pc %zu: %s %" PRId64 , vm->pc, desc->mnemonic, ins->imm);
    else
        LOG_V("pc %zu: %s", vm->pc, desc->mnemonic);
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

    vm->pc = ins->addr + 4;
    handler(vm, ctx, ins);
}

void wst_vm_close(WstVm *vm)
{
    if (vm == NULL)
        return;

    vm->pc = 0;
}
