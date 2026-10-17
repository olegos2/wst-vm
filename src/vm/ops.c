#include "toys/vm.h"
#include "vm_priv.h"
#include "toys/debug.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define VM_REG_READ(vm, r) ((r) == 0 ? 0 : (vm)->regs[(r)])

#define VM_REG_WRITE(vm, r, v) \
    do { \
        if ((r) != 0) \
            (vm)->regs[(r)] = (v); \
    } while (0)

/* Integer R-type op, `expr` sees `lhs`/`rhs` as uint64_t. */
#define VM_DEF_R_INT(name, expr) \
    void vm_exec_##name(WstVm *vm, WstVmContext *ctx, \
                        const WstVmInstr *ins) \
    { \
        (void)ctx; \
        uint64_t lhs = VM_REG_READ(vm, ins->rs1); \
        uint64_t rhs = VM_REG_READ(vm, ins->rs2); \
        VM_REG_WRITE(vm, ins->rd, (expr)); \
    }

/* Integer op with immediate, `expr` sees `lhs`/`rhs` as uint64_t. */
#define VM_DEF_R_INT_IMM(name, expr) \
    void vm_exec_##name(WstVm *vm, WstVmContext *ctx, \
                        const WstVmInstr *ins) \
    { \
        (void)ctx; \
        uint64_t lhs = VM_REG_READ(vm, ins->rs1); \
        uint64_t rhs = (uint64_t)ins->imm; \
        VM_REG_WRITE(vm, ins->rd, (expr)); \
    }

/* Float R-type op, `expr` sees `lhs`/`rhs` as double. */
#define VM_DEF_R_FP(name, expr) \
    void vm_exec_##name(WstVm *vm, WstVmContext *ctx, \
                        const WstVmInstr *ins) \
    { \
        (void)ctx; \
        double lhs = vm->fp[ins->rs1]; \
        double rhs = vm->fp[ins->rs2]; \
        vm->fp[ins->rd] = (expr); \
    }

/* Branch on signed compare, `cond` sees `lhs`/`rhs` as int64_t. */
#define VM_DEF_BRANCH(name, cond) \
    void vm_exec_##name(WstVm *vm, WstVmContext *ctx, \
                        const WstVmInstr *ins) \
    { \
        (void)ctx; \
        int64_t lhs = (int64_t)VM_REG_READ(vm, ins->rs1); \
        int64_t rhs = (int64_t)VM_REG_READ(vm, ins->rs2); \
        if (cond) \
            vm->pc = (size_t)((int64_t)ins->addr + ins->imm); \
    }

/* Float compare and write 0/1 into GPR, `cond` sees doubles. */
#define VM_DEF_FP_CMP(name, cond) \
    void vm_exec_##name(WstVm *vm, WstVmContext *ctx, \
                        const WstVmInstr *ins) \
    { \
        (void)ctx; \
        double lhs = vm->fp[ins->rs1]; \
        double rhs = vm->fp[ins->rs2]; \
        VM_REG_WRITE(vm, ins->rd, (cond) ? 1u : 0u); \
    }


static void vm_divmod(uint64_t a, uint64_t b, uint64_t *quot, uint64_t *rem)
{
    if (b == 0) {
        *quot = ~(uint64_t)0;
        *rem = a;
        return;
    }

    int64_t sign_a = (int64_t)a;
    int64_t sign_b = (int64_t)b;

    if (sign_a == INT64_MIN && sign_b == -1) {
        *quot = (uint64_t)INT64_MIN;
        *rem = 0;
        return;
    }

    *quot = (uint64_t)(sign_a / sign_b);
    *rem = (uint64_t)(sign_a % sign_b);
}

/* Double as raw bits, a union punning, no aliasing tricks. */
static uint64_t vm_fp_bits(double num)
{
    union {
        double num;
        uint64_t bits;
    } pun = { .num = num };

    return pun.bits;
}

/* Raw bits as a double, see `vm_fp_bits`. */
static double vm_fp_num(uint64_t bits)
{
    union {
        double num;
        uint64_t bits;
    } pun = { .bits = bits };

    return pun.num;
}

/* Exact comparison is intended here: feq follows RISC-V, where
 * NaN never equals and +0.0 equals -0.0. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"

static bool vm_fp_eq(double lhs, double rhs)
{
    return lhs == rhs;
}

#pragma GCC diagnostic pop


#define VM_STDIN_LINE_CAP (256)

static void vm_stdin_line(char *buf, size_t pc)
{
    assert(buf != NULL);

    fflush(stdout);
    if (fgets(buf, VM_STDIN_LINE_CAP, stdin) == NULL) {
        LOG_E("stdin eof at pc 0x%08zx", pc);
        exit(EXIT_FAILURE);
    }

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] != '\n' && !feof(stdin)) {
        LOG_E("stdin line over %zu bytes at pc 0x%08zx",
              VM_STDIN_LINE_CAP - 1, pc);
        exit(EXIT_FAILURE);
    }

    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
}

/* Trailing whitespace after the parsed number is fine, the rest is not. */
static bool vm_parse_end(const char *start, const char *end)
{
    assert(start != NULL);
    assert(end != NULL);

    if (end == start)
        return false;

    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
        end++;
    return *end == '\0';
}

void vm_exec_hlt(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)vm;
    (void)ctx;
    exit((int)ins->imm);
}

void vm_exec_outi(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    printf("%" PRId64 "\n", (int64_t)VM_REG_READ(vm, ins->rs1));
}

void vm_exec_outf(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    printf("%g\n", vm->fp[ins->rs1]);
}

void vm_exec_ini(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;

    char buf[VM_STDIN_LINE_CAP] = { 0 };
    vm_stdin_line(buf, ins->addr);

    char *end = NULL;
    errno = 0;
    long long val = strtoll(buf, &end, 0);
    if (errno != 0 || !vm_parse_end(buf, end)) {
        LOG_E("bad integer '%s' on stdin at pc 0x%08zx", buf, ins->addr);
        exit(EXIT_FAILURE);
    }
    VM_REG_WRITE(vm, ins->rd, (uint64_t)val);
}

void vm_exec_inf(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;

    char buf[VM_STDIN_LINE_CAP] = { 0 };
    vm_stdin_line(buf, ins->addr);

    char *end = NULL;
    errno = 0;
    double val = strtod(buf, &end);
    if (errno != 0 || !vm_parse_end(buf, end)) {
        LOG_E("bad double '%s' on stdin at pc 0x%08zx", buf, ins->addr);
        exit(EXIT_FAILURE);
    }
    vm->fp[ins->rd] = val;
}

void vm_exec_ld(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 8u;
    VM_REG_WRITE(vm, ins->rd, vm_mem_read64(ctx, addr));
}

void vm_exec_lw(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 4u;
    uint32_t raw = vm_mem_read32(ctx, addr);
    VM_REG_WRITE(vm, ins->rd, (U(64))(S(64))(S(32))raw);
}

void vm_exec_st(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 8u;
    vm_mem_write64(ctx, addr, VM_REG_READ(vm, ins->rs2));
}

void vm_exec_sw(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 4u;
    vm_mem_write32(ctx, addr, (uint32_t)VM_REG_READ(vm, ins->rs2));
}

void vm_exec_fld(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 8u;
    vm->fp[ins->rd] = vm_fp_num(vm_mem_read64(ctx, addr));
}

void vm_exec_fst(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 8u;
    vm_mem_write64(ctx, addr, vm_fp_bits(vm->fp[ins->rs2]));
}

void vm_exec_pushqr(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    vm_stack_push64(vm, ctx, ins->addr, VM_REG_READ(vm, ins->rs1));
}

void vm_exec_pushqv(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    vm_stack_push64(vm, ctx, ins->addr, vm_fp_bits(vm->fp[ins->rs1]));
}

void vm_exec_popqr(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    VM_REG_WRITE(vm, ins->rd, vm_stack_pop64(vm, ctx, ins->addr));
}

void vm_exec_popqv(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    vm->fp[ins->rd] = vm_fp_num(vm_stack_pop64(vm, ctx, ins->addr));
}

void vm_exec_jal(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    VM_REG_WRITE(vm, ins->rd, (uint64_t)(ins->addr + WST_VM_INSTR_SIZE));
    vm->pc = (size_t)((int64_t)ins->addr + ins->imm);
}

void vm_exec_jalr(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    uint64_t target = (VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm) & ~(uint64_t)1u;
    VM_REG_WRITE(vm, ins->rd, (uint64_t)(ins->addr + WST_VM_INSTR_SIZE));
    if (target % WST_VM_INSTR_SIZE != 0) {
        LOG_E("misaligned jalr target 0x%08" PRIx64 " at pc 0x%08zx",
              target, ins->addr);
        exit(EXIT_FAILURE);
    }
    vm->pc = (size_t)target;
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
VM_DEF_R_INT(sra, (U(64))((S(64))lhs >> (rhs & 63u)))
VM_DEF_R_INT(divu, rhs == 0 ? ~(uint64_t)0 : lhs / rhs)
VM_DEF_R_INT(remu, rhs == 0 ? lhs : lhs % rhs)

void vm_exec_div(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    uint64_t lhs = VM_REG_READ(vm, ins->rs1);
    uint64_t rhs = VM_REG_READ(vm, ins->rs2);
    uint64_t quot = 0, rem = 0;
    vm_divmod(lhs, rhs, &quot, &rem);
    VM_REG_WRITE(vm, ins->rd, quot);
}

void vm_exec_rem(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    uint64_t lhs = VM_REG_READ(vm, ins->rs1);
    uint64_t rhs = VM_REG_READ(vm, ins->rs2);
    uint64_t quot = 0, rem = 0;
    vm_divmod(lhs, rhs, &quot, &rem);
    VM_REG_WRITE(vm, ins->rd, rem);
}

void vm_exec_addi(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    uint64_t lhs = VM_REG_READ(vm, ins->rs1);
    VM_REG_WRITE(vm, ins->rd, lhs + (uint64_t)ins->imm);
}

void vm_exec_lui(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    VM_REG_WRITE(vm, ins->rd, (uint64_t)ins->imm);
}

void vm_exec_auipc(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    VM_REG_WRITE(vm, ins->rd, (uint64_t)ins->addr + (uint64_t)ins->imm);
}

VM_DEF_R_INT_IMM(andi, lhs & rhs)
VM_DEF_R_INT_IMM(ori, lhs | rhs)
VM_DEF_R_INT_IMM(xori, lhs ^ rhs)
VM_DEF_R_INT_IMM(slli, lhs << (rhs & 63u))
VM_DEF_R_INT_IMM(srli, lhs >> (rhs & 63u))
VM_DEF_R_INT_IMM(srai, (U(64))((S(64))lhs >> (rhs & 63u)))
VM_DEF_R_INT_IMM(slti, ((S(64))lhs < (S(64))rhs) ? 1u : 0u)
VM_DEF_R_INT_IMM(sltiu, (lhs < rhs) ? 1u : 0u)

VM_DEF_R_FP(fadd, lhs + rhs)
VM_DEF_R_FP(fsub, lhs - rhs)
VM_DEF_R_FP(fmul, lhs * rhs)
VM_DEF_R_FP(fdiv, lhs / rhs)

void vm_exec_fsqrt(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    (void)ctx;
    vm->fp[ins->rd] = sqrt(vm->fp[ins->rs1]);
}

VM_DEF_FP_CMP(feq, vm_fp_eq(lhs, rhs))
VM_DEF_FP_CMP(flt, lhs < rhs)
VM_DEF_FP_CMP(fle, lhs <= rhs)

void vm_sys_exit(WstVm *vm, WstVmContext *ctx, size_t addr)
{
    (void)ctx;
    (void)addr;
    exit((int)(ECALL_ARG0 & 0xFFu));
}

void vm_sys_write(WstVm *vm, WstVmContext *ctx, size_t addr)
{
    uint64_t fd = ECALL_ARG0;
    uint64_t buf = ECALL_ARG1;
    uint64_t len = ECALL_ARG2;

    if (fd != 1 && fd != 2) {
        LOG_E("ecall write to fd %" PRIu64 " at pc 0x%08zx", fd, addr);
        exit(EXIT_FAILURE);
    }

    if (len > ctx->mem_size || buf > ctx->mem_size - len) {
        LOG_E("ecall write of 0x%08" PRIx64 " bytes at 0x%08" PRIx64
              " past mem end 0x%08zx at pc 0x%08zx",
              len, buf, ctx->mem_size, addr);
        exit(EXIT_FAILURE);
    }

    /* Flush our own buffered output first, or `write` breaks it. */
    fflush(stdout);

    ssize_t done = write((int)fd, &ctx->mem[(size_t)buf], (size_t)len);
    ECALL_RET = (uint64_t)done;
}

void vm_exec_ecall(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t num = ECALL_NUM;

    /* out-of-range numbers fall out, NULL entries are unknown. */
    WstVmSyscallHandler handler =
        num < WST_VM_SYSCALL_COUNT ? vm_syscalls[(size_t)num] : NULL;
    if (handler == NULL) {
        LOG_E("unknown syscall %" PRIu64 " at pc 0x%08zx", num, ins->addr);
        exit(EXIT_FAILURE);
    }

    handler(vm, ctx, ins->addr);
}

void vm_exec_call(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    vm_stack_push64(vm, ctx, ins->addr,
                    (uint64_t)(ins->addr + WST_VM_INSTR_SIZE));
    vm->pc = (size_t)((int64_t)ins->addr + ins->imm);
}

void vm_exec_ret(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)
{
    uint64_t target = vm_stack_pop64(vm, ctx, ins->addr);
    if (target % WST_VM_INSTR_SIZE != 0) {
        LOG_E("misaligned ret target 0x%08" PRIx64 " at pc 0x%08zx",
              target, ins->addr);
        exit(EXIT_FAILURE);
    }
    vm->pc = (size_t)target;
}
