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
    if (size > ctx->mem_size || addr > (uint64_t)ctx->mem_size - size) {
        LOG_E("address %" PRIu64 " past address space end", addr);
        exit(EXIT_FAILURE);
    }
}

/* Size-specific accessors, misaligned access is fatal. */
#define VM_DEF_MEM_RW(bits) \
    U(bits) vm_mem_read##bits(WstVmContext *ctx, uint64_t addr) \
    { \
        vm_mem_ensure(ctx, addr, (bits) / 8); \
        if (addr % ((bits) / 8) != 0) { \
            LOG_E("misaligned address %" PRIu64, addr); \
            exit(EXIT_FAILURE); \
        } \
        return *(U(bits) *)&ctx->mem[(size_t)addr]; \
    } \
    \
    void vm_mem_write##bits(WstVmContext *ctx, uint64_t addr, U(bits) value) \
    { \
        vm_mem_ensure(ctx, addr, (bits) / 8); \
        if (addr % ((bits) / 8) != 0) { \
            LOG_E("misaligned address %" PRIu64, addr); \
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
        LOG_E("stack of %zu bytes past address space end", stack_size);
        return false;
    }
    stack_size = (stack_size + 7) & ~(size_t)7;

    uint8_t *mem = calloc(limit, sizeof(uint8_t));
    if (mem == NULL) {
        LOG_E("cannot reserve %zu bytes", limit);
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

bool wst_vm_decode(const WstVm *vm, const WstVmContext *ctx, WstVmInstr *ins)
{
    assert(vm != NULL);
    assert(ctx != NULL);
    assert(ins != NULL);

    if (vm->pc % 4 != 0) {
        LOG_E("misaligned pc %zu", vm->pc);
        return false;
    }

    if (vm->pc >= ctx->mem_size || ctx->mem_size - vm->pc < 4) {
        LOG_E("pc %zu past program end %zu", vm->pc, ctx->mem_size);
        return false;
    }

    /* Fetch never grows memory, past-end is a decode error (checked above).
     * `pc` is 4-aligned here, so this is an aligned dereference. */
    uint32_t word = *(const uint32_t *)&ctx->mem[vm->pc];

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
