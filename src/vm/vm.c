#include "toys/vm.h"
#include "toys/debug.h"
#include "toys/prog.h"
#include "toys/vec_internal.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


/* x0 reads as zero and drops writes, single-eval on reg fields. */
#define VM_REG_READ(vm, r) ((r) == 0 ? 0 : (vm)->regs[(r)])

/* Statement macro, do-while(0) like VEC_SEAL. */
#define VM_REG_WRITE(vm, r, v) \
    do { \
        if ((r) != 0) \
            (vm)->regs[(r)] = (v); \
    } while (0)

/* Integer R-type op, `expr` sees `lhs`/`rhs` as uint64_t. */
#define VM_DEF_R_INT(name, expr) \
    static void vm_exec_##name(WstVm *vm, WstVmContext *ctx, \
                               const WstVmInstr *ins) \
    { \
        (void)ctx; \
        uint64_t lhs = VM_REG_READ(vm, ins->rs1); \
        uint64_t rhs = VM_REG_READ(vm, ins->rs2); \
        VM_REG_WRITE(vm, ins->rd, (expr)); \
    }

/* Float R-type op, `expr` sees `lhs`/`rhs` as double. */
#define VM_DEF_R_FP(name, expr) \
    static void vm_exec_##name(WstVm *vm, WstVmContext *ctx, \
                               const WstVmInstr *ins) \
    { \
        (void)ctx; \
        double lhs = vm->fp[ins->rs1]; \
        double rhs = vm->fp[ins->rs2]; \
        vm->fp[ins->rd] = (expr); \
    }

/* Branch on signed compare, `cond` sees `lhs`/`rhs` as int64_t. */
#define VM_DEF_BRANCH(name, cond) \
    static void vm_exec_##name(WstVm *vm, WstVmContext *ctx, \
                               const WstVmInstr *ins) \
    { \
        (void)ctx; \
        int64_t lhs = (int64_t)VM_REG_READ(vm, ins->rs1); \
        int64_t rhs = (int64_t)VM_REG_READ(vm, ins->rs2); \
        if (cond) \
            vm->pc = (size_t)((int64_t)ins->addr + ins->imm); \
    }

/* Float compare writing 0/1 into GPR, `cond` sees doubles. */
#define VM_DEF_FP_CMP(name, cond) \
    static void vm_exec_##name(WstVm *vm, WstVmContext *ctx, \
                               const WstVmInstr *ins) \
    { \
        (void)ctx; \
        double lhs = vm->fp[ins->rs1]; \
        double rhs = vm->fp[ins->rs2]; \
        VM_REG_WRITE(vm, ins->rd, (cond) ? 1u : 0u); \
    }


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

static uint64_t vm_mem_read(WstVmContext *ctx, uint64_t addr, unsigned width)
{
    vm_mem_ensure(ctx, addr, width);

    uint64_t value = 0;
    memcpy(&value, &ctx->mem.data[(size_t)addr], width);
    return value;
}

static void vm_mem_write(WstVmContext *ctx, uint64_t addr, uint64_t value, unsigned width)
{
    vm_mem_ensure(ctx, addr, width);

    memcpy(&ctx->mem.data[(size_t)addr], &value, width);
    VEC_SEAL((WstVecVoid *)&ctx->mem, sizeof(uint8_t));
}

static void vm_push_u64(WstVm *vm, size_t pc, uint64_t value)
{
    double bits = 0.0;
    memcpy(&bits, &value, sizeof(bits));
    WstVecErr err = wst_vec_double_push(&vm->stack, bits);
    if (err != WST_VEC_NO_ERR) {
        LOG_E("stack push failed at pc %zu: %s", pc, wst_vec_err_str(err));
        exit(EXIT_FAILURE);
    }
}

static uint64_t vm_pop_u64(WstVm *vm, size_t pc)
{
    double bits = 0.0;
    if (wst_vec_double_pop(&vm->stack, &bits) != WST_VEC_NO_ERR) {
        LOG_E("stack underflow at pc %zu", pc);
        exit(EXIT_FAILURE);
    }
    uint64_t value = 0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void vm_divmod(uint64_t dividend, uint64_t divisor, uint64_t *quot, uint64_t *rem)
{
    if (divisor == 0) {
        *quot = ~(uint64_t)0;
        *rem = dividend;
        return;
    }

    int64_t signed_dividend = (int64_t)dividend;
    int64_t signed_divisor = (int64_t)divisor;

    if (signed_dividend == INT64_MIN && signed_divisor == -1) {
        *quot = (uint64_t)INT64_MIN;
        *rem = 0;
        return;
    }

    *quot = (uint64_t)(signed_dividend / signed_divisor);
    *rem = (uint64_t)(signed_dividend % signed_divisor);
}

/* Exact comparison is deliberate here: feq follows RISC-V, where
 * NaN never equals and +0.0 equals -0.0. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"

static bool vm_fp_eq(double lhs, double rhs)
{
    return lhs == rhs;
}

#pragma GCC diagnostic pop

static void vm_exec_hlt(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)vm;
    (void)ctx;
    exit((int)ins->imm);
}

static void vm_exec_outi(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    printf("%" PRId64 "\n", (int64_t)VM_REG_READ(vm, ins->rs1));
}

static void vm_exec_outf(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    printf("%g\n", vm->fp[ins->rs1]);
}

static void vm_exec_ld(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm;
    VM_REG_WRITE(vm, ins->rd, vm_mem_read(ctx, addr, 8));
}

static void vm_exec_lw(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm;
    uint64_t raw = vm_mem_read(ctx, addr, 4);
    VM_REG_WRITE(vm, ins->rd, (uint64_t)(int64_t)(int32_t)(uint32_t)raw);
}

static void vm_exec_st(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm;
    vm_mem_write(ctx, addr, VM_REG_READ(vm, ins->rs2), 8);
}

static void vm_exec_sw(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm;
    uint32_t low = (uint32_t)VM_REG_READ(vm, ins->rs2);
    vm_mem_write(ctx, addr, (uint64_t)low, 4);
}

static void vm_exec_fld(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm;
    uint64_t bits = vm_mem_read(ctx, addr, 8);
    memcpy(&vm->fp[ins->rd], &bits, sizeof(bits));
}

static void vm_exec_fst(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm;
    uint64_t bits = 0;
    memcpy(&bits, &vm->fp[ins->rs2], sizeof(bits));
    vm_mem_write(ctx, addr, bits, 8);
}

static void vm_exec_pushqr(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    vm_push_u64(vm, ins->addr, VM_REG_READ(vm, ins->rs1));
}

static void vm_exec_pushqv(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    uint64_t bits = 0;
    memcpy(&bits, &vm->fp[ins->rs1], sizeof(bits));
    vm_push_u64(vm, ins->addr, bits);
}

static void vm_exec_popqr(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    VM_REG_WRITE(vm, ins->rd, vm_pop_u64(vm, ins->addr));
}

static void vm_exec_popqv(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    uint64_t bits = vm_pop_u64(vm, ins->addr);
    memcpy(&vm->fp[ins->rd], &bits, sizeof(bits));
}

static void vm_exec_jal(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    VM_REG_WRITE(vm, ins->rd, (uint64_t)(ins->addr + 4));
    vm->pc = (size_t)((int64_t)ins->addr + ins->imm);
}

VM_DEF_BRANCH(beq, lhs == rhs)
VM_DEF_BRANCH(bne, lhs != rhs)
VM_DEF_BRANCH(blt, lhs < rhs)
VM_DEF_BRANCH(bge, lhs >= rhs)

VM_DEF_R_INT(add, lhs + rhs)
VM_DEF_R_INT(sub, lhs - rhs)
VM_DEF_R_INT(mul, lhs * rhs)
VM_DEF_R_INT(and, lhs & rhs)
VM_DEF_R_INT(or, lhs | rhs)
VM_DEF_R_INT(xor, lhs ^ rhs)
VM_DEF_R_INT(sll, lhs << (rhs & 63u))
VM_DEF_R_INT(srl, lhs >> (rhs & 63u))
VM_DEF_R_INT(sra, (uint64_t)((int64_t)lhs >> (rhs & 63u)))

static void vm_exec_div(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    uint64_t lhs = VM_REG_READ(vm, ins->rs1);
    uint64_t rhs = VM_REG_READ(vm, ins->rs2);
    uint64_t quot = 0, rem = 0;
    vm_divmod(lhs, rhs, &quot, &rem);
    VM_REG_WRITE(vm, ins->rd, quot);
}

static void vm_exec_mod(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    uint64_t lhs = VM_REG_READ(vm, ins->rs1);
    uint64_t rhs = VM_REG_READ(vm, ins->rs2);
    uint64_t quot = 0, rem = 0;
    vm_divmod(lhs, rhs, &quot, &rem);
    VM_REG_WRITE(vm, ins->rd, rem);
}

static void vm_exec_addi(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    uint64_t lhs = VM_REG_READ(vm, ins->rs1);
    VM_REG_WRITE(vm, ins->rd, lhs + (uint64_t)ins->imm);
}

static void vm_exec_lui(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    VM_REG_WRITE(vm, ins->rd, (uint64_t)ins->imm);
}

VM_DEF_R_FP(fadd, lhs + rhs)
VM_DEF_R_FP(fsub, lhs - rhs)
VM_DEF_R_FP(fmul, lhs * rhs)
VM_DEF_R_FP(fdiv, lhs / rhs)

static void vm_exec_fsqrt(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    vm->fp[ins->rd] = sqrt(vm->fp[ins->rs1]);
}

static void vm_exec_fpow(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    vm->fp[ins->rd] = pow(vm->fp[ins->rs1], vm->fp[ins->rs2]);
}

VM_DEF_FP_CMP(feq, vm_fp_eq(lhs, rhs))
VM_DEF_FP_CMP(flt, lhs < rhs)
VM_DEF_FP_CMP(fle, lhs <= rhs)

typedef void (*WstVmSyscallHandler)(WstVm *vm, WstVmContext *ctx, size_t addr);

static void vm_sys_exit(WstVm *vm, WstVmContext *ctx, size_t addr)
{
    (void)ctx;
    (void)addr;
    exit((int)(vm->regs[1] & 0xFFu));
}

static void vm_sys_write(WstVm *vm, WstVmContext *ctx, size_t addr)
{
    uint64_t fd = vm->regs[1];
    uint64_t buf = vm->regs[2];
    uint64_t len = vm->regs[3];

    if (fd != 1 && fd != 2) {
        LOG_E("ecall write to fd %llu at pc %zu", (unsigned long long)fd, addr);
        exit(EXIT_FAILURE);
    }

    if (len > ctx->mem.length || buf > ctx->mem.length - len) {
        LOG_E("ecall write past address space end at pc %zu", addr);
        exit(EXIT_FAILURE);
    }

    ssize_t done = write((int)fd, &ctx->mem.data[(size_t)buf], (size_t)len);
    VM_REG_WRITE(vm, 1, (uint64_t)done);
}

static const WstVmSyscallHandler vm_syscalls[] = {
    [WST_VM_SYS_EXIT] = vm_sys_exit,
    [WST_VM_SYS_WRITE] = vm_sys_write,
};

#define VM_SYSCALL_COUNT (sizeof(vm_syscalls) / sizeof(*vm_syscalls))

static void vm_exec_ecall(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t num = (uint64_t)ins->imm;

    if (num >= VM_SYSCALL_COUNT || vm_syscalls[(size_t)num] == NULL) {
        LOG_E("unknown syscall %llu at pc %zu", (unsigned long long)num, ins->addr);
        exit(EXIT_FAILURE);
    }

    vm_syscalls[(size_t)num](vm, ctx, ins->addr);
}

static void vm_exec_call(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    vm_push_u64(vm, ins->addr, (uint64_t)(ins->addr + 4));
    vm->pc = (size_t)((int64_t)ins->addr + ins->imm);
}

static void vm_exec_ret(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    uint64_t target = vm_pop_u64(vm, ins->addr);
    if (target % 4 != 0) {
        LOG_E("misaligned ret target %llu", (unsigned long long)target);
        exit(EXIT_FAILURE);
    }
    vm->pc = (size_t)target;
}

const WstVmInstrDesc wst_vm_op_table[WST_VM_OPCODE_COUNT] = {
    [WST_VM_HLT]      = { "hlt",      WST_VM_INSTR_FORMAT_I, WST_VM_OPND_IMM, vm_exec_hlt },
    [WST_VM_OUTI]     = { "outi",     WST_VM_INSTR_FORMAT_I, WST_VM_OPND_RS1, vm_exec_outi },
    [WST_VM_OUTF]     = { "outf",     WST_VM_INSTR_FORMAT_I, WST_VM_OPND_RS1, vm_exec_outf },
    [WST_VM_LD]       = { "ld",       WST_VM_INSTR_FORMAT_I, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM, vm_exec_ld },
    [WST_VM_LW]       = { "lw",       WST_VM_INSTR_FORMAT_I, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM, vm_exec_lw },
    [WST_VM_ST]       = { "st",       WST_VM_INSTR_FORMAT_S, WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM, vm_exec_st },
    [WST_VM_SW]       = { "sw",       WST_VM_INSTR_FORMAT_S, WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM, vm_exec_sw },
    [WST_VM_FLD]      = { "fld",      WST_VM_INSTR_FORMAT_I, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM, vm_exec_fld },
    [WST_VM_FST]      = { "fst",      WST_VM_INSTR_FORMAT_S, WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM, vm_exec_fst },
    [WST_VM_PUSHQR]   = { "pushqr",   WST_VM_INSTR_FORMAT_I, WST_VM_OPND_RS1, vm_exec_pushqr },
    [WST_VM_PUSHQV]   = { "pushqv",   WST_VM_INSTR_FORMAT_I, WST_VM_OPND_RS1, vm_exec_pushqv },
    [WST_VM_POPQR]    = { "popqr",    WST_VM_INSTR_FORMAT_I, WST_VM_OPND_RD,  vm_exec_popqr },
    [WST_VM_POPQV]    = { "popqv",    WST_VM_INSTR_FORMAT_I, WST_VM_OPND_RD,  vm_exec_popqv },
    [WST_VM_JAL]      = { "jal",      WST_VM_INSTR_FORMAT_J, WST_VM_OPND_RD  | WST_VM_OPND_IMM, vm_exec_jal },
    [WST_VM_BEQ]      = { "beq",      WST_VM_INSTR_FORMAT_B, WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM, vm_exec_beq },
    [WST_VM_BNE]      = { "bne",      WST_VM_INSTR_FORMAT_B, WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM, vm_exec_bne },
    [WST_VM_BLT]      = { "blt",      WST_VM_INSTR_FORMAT_B, WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM, vm_exec_blt },
    [WST_VM_BGE]      = { "bge",      WST_VM_INSTR_FORMAT_B, WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM, vm_exec_bge },
    [WST_VM_ADD]      = { "add",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_add },
    [WST_VM_SUB]      = { "sub",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_sub },
    [WST_VM_MUL]      = { "mul",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_mul },
    [WST_VM_DIV]      = { "div",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_div },
    [WST_VM_MOD]      = { "mod",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_mod },
    [WST_VM_AND]      = { "and",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_and },
    [WST_VM_OR]       = { "or",       WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_or },
    [WST_VM_XOR]      = { "xor",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_xor },
    [WST_VM_SLL]      = { "sll",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_sll },
    [WST_VM_SRL]      = { "srl",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_srl },
    [WST_VM_SRA]      = { "sra",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_sra },
    [WST_VM_ADDI]     = { "addi",     WST_VM_INSTR_FORMAT_I, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM, vm_exec_addi },
    [WST_VM_LUI]      = { "lui",      WST_VM_INSTR_FORMAT_U, WST_VM_OPND_RD  | WST_VM_OPND_IMM, vm_exec_lui },
    [WST_VM_FADD]     = { "fadd",     WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fadd },
    [WST_VM_FSUB]     = { "fsub",     WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fsub },
    [WST_VM_FMUL]     = { "fmul",     WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fmul },
    [WST_VM_FDIV]     = { "fdiv",     WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fdiv },
    [WST_VM_FSQRT]    = { "fsqrt",    WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1, vm_exec_fsqrt },
    [WST_VM_FPOW]     = { "fpow",     WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fpow },
    [WST_VM_FEQ]      = { "feq",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_feq },
    [WST_VM_FLT]      = { "flt",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_flt },
    [WST_VM_FLE]      = { "fle",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fle },
    [WST_VM_ECALL]    = { "ecall",    WST_VM_INSTR_FORMAT_I, WST_VM_OPND_IMM, vm_exec_ecall },
    [WST_VM_CALL]     = { "call",     WST_VM_INSTR_FORMAT_J, WST_VM_OPND_IMM, vm_exec_call },
    [WST_VM_RET]      = { "ret",      WST_VM_INSTR_FORMAT_R, WST_VM_OPND_NONE, vm_exec_ret },
};

bool wst_vm_init(WstVm *vm)
{
    assert(vm != NULL);

    for (size_t i = 0; i < 8; i++) {
        vm->regs[i] = 0;
        vm->fp[i] = 0.0;
    }
    vm->pc = 0;

    WstVecErr err = wst_vec_double_init(&vm->stack, 0);
    if (err != WST_VEC_NO_ERR) {
        LOG_E("cannot init value stack: %s", wst_vec_err_str(err));
        return false;
    }
    return true;
}

bool wst_vm_context_load(WstVmContext *ctx, const char *path)
{
    assert(ctx != NULL);
    assert(path != NULL);

    wst_vec_u8_free(&ctx->mem);
    ctx->entry = 0;

    size_t entry = 0;
    if (!wst_prog_load(&ctx->mem, &entry, path, vm_mem_limit(ctx)))
        return false;

    ctx->entry = entry;
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

    /* Decode only yields validated ops, so every reachable row has a handler. */
    WstVmHandler handler = wst_vm_op_table[(unsigned)ins->op].handler;
    assert(handler != NULL);

    vm->pc = ins->addr + 4;
    handler(vm, ctx, ins);
}

void wst_vm_close(WstVm *vm)
{
    if (vm == NULL)
        return;

    WstVecErr err = wst_vec_double_free(&vm->stack);
    if (err != WST_VEC_NO_ERR)
        LOG_W("value stack damaged: %s", wst_vec_err_str(err));
    vm->pc = 0;
}
