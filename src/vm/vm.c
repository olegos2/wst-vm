#include "toys/vm.h"
#include "vm_priv.h"
#include "toys/debug.h"
#include "toys/prog.h"
#include "toys/vec_internal.h"

#include <assert.h>
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

    return ctx->stack_size != 0 ? ctx->stack_size : WST_VM_STACK_DEFAULT_SIZE;
}

static void vm_mem_ensure(WstVmContext *ctx, uint64_t addr, uint64_t size)
{
    size_t limit = vm_mem_limit(ctx);
    if (size > limit || addr > (uint64_t)limit - size) {
        LOG_E("address %llu past address space end", (unsigned long long)addr);
        exit(EXIT_FAILURE);
    }

    if (wst_vec_u8_ensure(&ctx->mem, (size_t)(addr + size)) != WST_VEC_NO_ERR) {
        LOG_E("cannot grow address space");
        exit(EXIT_FAILURE);
    }
}

uint64_t vm_mem_read(WstVmContext *ctx, uint64_t addr, unsigned width)
{
    vm_mem_ensure(ctx, addr, width);

    uint64_t value = 0;
    memcpy(&value, &ctx->mem.data[(size_t)addr], width);
    return value;
}

void vm_mem_write(WstVmContext *ctx, uint64_t addr, uint64_t value, unsigned width)
{
    vm_mem_ensure(ctx, addr, width);

    memcpy(&ctx->mem.data[(size_t)addr], &value, width);
    VEC_SEAL((WstVecVoid *)&ctx->mem, sizeof(uint8_t));
}

/* Push 8 bytes to stack, `sp` grows down. */
void vm_stack_push(WstVm *vm, WstVmContext *ctx, size_t pc, uint64_t value)
{
    uint64_t sp = vm->regs[WST_VM_REG_SP];

    if (sp % 8 != 0) {
        LOG_E("misaligned sp %llu at pc %zu", (unsigned long long)sp, pc);
        exit(EXIT_FAILURE);
    }
    if (sp < (uint64_t)ctx->stack_base + 8 || sp > (uint64_t)ctx->stack_top) {
        LOG_E("stack overflow at pc %zu", pc);
        exit(EXIT_FAILURE);
    }

    vm->regs[WST_VM_REG_SP] = sp - 8;
    vm_mem_write(ctx, sp - 8, value, 8);
}

/* Pop 8 bytes from stack */
uint64_t vm_stack_pop(WstVm *vm, WstVmContext *ctx, size_t pc)
{
    uint64_t sp = vm->regs[WST_VM_REG_SP];

    if (sp % 8 != 0) {
        LOG_E("misaligned sp %llu at pc %zu", (unsigned long long)sp, pc);
        exit(EXIT_FAILURE);
    }
    if (sp >= (uint64_t)ctx->stack_top || sp < (uint64_t)ctx->stack_base) {
        LOG_E("stack underflow at pc %zu", pc);
        exit(EXIT_FAILURE);
    }

    vm->regs[WST_VM_REG_SP] = sp + 8;
    return vm_mem_read(ctx, sp, 8);
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
    ctx->stack_base = 0;
    ctx->stack_top = 0;

    size_t entry = 0;
    if (!wst_prog_load(&ctx->mem, &entry, path, vm_mem_limit(ctx)))
        return false;

    if (ctx->mem.length > SIZE_MAX - 8)
        return false;

    size_t base = (ctx->mem.length + 7) & ~(size_t)7;
    size_t ssize = vm_stack_size(ctx);
    size_t limit = vm_mem_limit(ctx);
    if (ssize > limit || base > limit - ssize) {
        LOG_E("stack of %zu bytes past address space end", ssize);
        return false;
    }
    if (wst_vec_u8_resize(&ctx->mem, base + ssize) != WST_VEC_NO_ERR)
        return false;

    ctx->entry = entry;
    ctx->stack_base = base;
    ctx->stack_top = base + ssize;
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
    ctx->stack_base = 0;
    ctx->stack_top = 0;
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

    uint32_t word = 0;
    memcpy(&word, &ctx->mem.data[vm->pc], sizeof(word));

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
        LOG_V("pc %zu: %s %lli", vm->pc, desc->mnemonic, (long long)ins->imm);
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
