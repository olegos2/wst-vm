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


/* Single values live in the low 32 bits, upper bits mark the box. */
#define VM_F32_BOX_MASK UINT64_C(0xFFFFFFFF00000000)
#define VM_F32_CANON_NAN UINT32_C(0x7fc00000)

#define VM_REG_READ(vm, r) ((r) == 0 ? 0 : (vm)->regs[(r)])

#define VM_REG_WRITE(vm, r, v) \
    do { \
        if ((r) != 0) \
            (vm)->regs[(r)] = (v); \
    } while (0)

/* Integer R-type op, `expr` sees `lhs`/`rhs` as uint64_t. */
#define VM_DEF_R_INT(name, expr) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        uint64_t lhs = VM_REG_READ(vm, ins->rs1); \
        uint64_t rhs = VM_REG_READ(vm, ins->rs2); \
        VM_REG_WRITE(vm, ins->rd, (expr)); \
    }

/* Integer op with immediate, `expr` sees `lhs`/`rhs` as uint64_t. */
#define VM_DEF_R_INT_IMM(name, expr) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        uint64_t lhs = VM_REG_READ(vm, ins->rs1); \
        uint64_t rhs = (uint64_t)ins->imm; \
        VM_REG_WRITE(vm, ins->rd, (expr)); \
    }

/* Float R-type op, `expr` sees `lhs`/`rhs` as double. */
#define VM_DEF_R_FP(name, expr) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        double lhs = vm->fp[ins->rs1]; \
        double rhs = vm->fp[ins->rs2]; \
        vm->fp[ins->rd] = (expr); \
    }

/* Branch on signed compare, `cond` sees `lhs`/`rhs` as int64_t. */
#define VM_DEF_BRANCH(name, cond) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        int64_t lhs = (int64_t)VM_REG_READ(vm, ins->rs1); \
        int64_t rhs = (int64_t)VM_REG_READ(vm, ins->rs2); \
        if (cond) \
            vm->pc = (size_t)((int64_t)ins->addr + ins->imm); \
    }

/* Float compare and write 0/1 into GPR, `cond` sees doubles. */
#define VM_DEF_FP_CMP(name, cond) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        double lhs = vm->fp[ins->rs1]; \
        double rhs = vm->fp[ins->rs2]; \
        VM_REG_WRITE(vm, ins->rd, (cond) ? 1u : 0u); \
    }

/* Integer R-type op on the low 32 bits, sign-extend the result. */
#define VM_DEF_R_W(name, expr) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        uint32_t lhs = (uint32_t)VM_REG_READ(vm, ins->rs1); \
        uint32_t rhs = (uint32_t)VM_REG_READ(vm, ins->rs2); \
        VM_REG_WRITE(vm, ins->rd, vm_sext32((uint32_t)(expr))); \
    }

/* Integer op with immediate on the low 32 bits, sign-extend the result. */
#define VM_DEF_R_W_IMM(name, expr) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        uint32_t lhs = (uint32_t)VM_REG_READ(vm, ins->rs1); \
        uint32_t rhs = (uint32_t)ins->imm; \
        VM_REG_WRITE(vm, ins->rd, vm_sext32((uint32_t)(expr))); \
    }

/* Single-precision R-type op, operands and result are NaN-boxed. */
#define VM_DEF_R_FP32(name, expr) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        float lhs = vm_f32_read(vm, ins->rs1); \
        float rhs = vm_f32_read(vm, ins->rs2); \
        vm_f32_write(vm, ins->rd, vm_f32_bits((float)(expr))); \
    }

#define VM_DEF_R_FP32_1(name, expr) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        float src = vm_f32_read(vm, ins->rs1); \
        vm_f32_write(vm, ins->rd, vm_f32_bits((float)(expr))); \
    }

/* Fused multiply-add R4 op, `expr` sees double `a`, `b`, `c`. */
#define VM_DEF_R4_FP(name, expr) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        double a = vm->fp[ins->rs1]; \
        double b = vm->fp[ins->rs2]; \
        double c = vm->fp[ins->rs3]; \
        vm->fp[ins->rd] = (expr); \
    }

/* Single-precision fused multiply-add, operands and result are boxed. */
#define VM_DEF_R4_FP32(name, expr) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        float a = vm_f32_read(vm, ins->rs1); \
        float b = vm_f32_read(vm, ins->rs2); \
        float c = vm_f32_read(vm, ins->rs3); \
        vm_f32_write(vm, ins->rd, vm_f32_bits((float)(expr))); \
    }

/* Single-precision compare and write 0/1 into a GPR. */
#define VM_DEF_FP32_CMP(name, cond) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        float lhs = vm_f32_read(vm, ins->rs1); \
        float rhs = vm_f32_read(vm, ins->rs2); \
        VM_REG_WRITE(vm, ins->rd, (cond) ? 1u : 0u); \
    }


/* Sign-extend the low 32 bits, the result model of the W ops. */
static uint64_t vm_sext32(uint32_t value)
{
    return (uint64_t)(int64_t)(int32_t)value;
}

static float vm_f32_num(uint32_t bits)
{
    union {
        uint32_t bits;
        float num;
    } pun = { .bits = bits };

    return pun.num;
}

static uint32_t vm_f32_bits(float num)
{
    union {
        uint32_t bits;
        float num;
    } pun = { .num = num };

    return pun.bits;
}

/* Single to signed word, RISC-V style saturation instead of undefined. */
static int32_t vm_f32_to_i32(float value)
{
    if (isnan(value))
        return INT32_MAX;
    if (value >= 2147483648.0f)
        return INT32_MAX;
    if (value <= -2147483648.0f)
        return INT32_MIN;
    return (int32_t)value;
}

static void vm_divmodw(uint32_t a, uint32_t b, uint32_t *quot, uint32_t *rem)
{
    if (b == 0) {
        *quot = UINT32_MAX;
        *rem = a;
        return;
    }

    int32_t sign_a = (int32_t)a;
    int32_t sign_b = (int32_t)b;

    if (sign_a == INT32_MIN && sign_b == -1) {
        *quot = (uint32_t)INT32_MIN;
        *rem = 0;
        return;
    }

    *quot = (uint32_t)(sign_a / sign_b);
    *rem = (uint32_t)(sign_a % sign_b);
}

static void vm_divmoduw(uint32_t a, uint32_t b, uint32_t *quot, uint32_t *rem)
{
    if (b == 0) {
        *quot = UINT32_MAX;
        *rem = a;
        return;
    }

    *quot = a / b;
    *rem = a % b;
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

/* Double as raw bits, no aliasing tricks. */
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

/* True when the upper 32 bits hold the single-precision box marker. */
static bool vm_f32_is_boxed(uint64_t bits)
{
    return (bits & VM_F32_BOX_MASK) == VM_F32_BOX_MASK;
}

/* Single bits as they appear in a register, non-boxed reads as NaN. */
static uint32_t vm_f32_read_bits(const WstVm *vm, unsigned reg)
{
    uint64_t bits = vm_fp_bits(vm->fp[reg]);
    return vm_f32_is_boxed(bits) ? (uint32_t)bits : VM_F32_CANON_NAN;
}

/* Single value of a register, non-boxed reads as the canonical NaN. */
static float vm_f32_read(const WstVm *vm, unsigned reg)
{
    return vm_f32_num(vm_f32_read_bits(vm, reg));
}

static void vm_f32_write(WstVm *vm, unsigned reg, uint32_t bits)
{
    vm->fp[reg] = vm_fp_num(VM_F32_BOX_MASK | (uint64_t)bits);
}

/* Exact comparison is intended here: feq follows RISC-V, where
 * NaN never equals and +0.0 equals -0.0. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"

static bool vm_fp_eq(double lhs, double rhs)
{
    return lhs == rhs;
}

static bool vm_fp_eq32(float lhs, float rhs)
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

void vm_exec_outi(WstVm *vm, const WstVmInstr *ins)
{
    printf("%" PRId64 "\n", (int64_t)VM_REG_READ(vm, ins->rs1));
}

void vm_exec_outf(WstVm *vm, const WstVmInstr *ins)
{

    uint64_t bits = vm_fp_bits(vm->fp[ins->rs1]);
    if (vm_f32_is_boxed(bits))
        printf("%g\n", (double)vm_f32_num((uint32_t)bits));
    else
        printf("%g\n", vm->fp[ins->rs1]);
}

void vm_exec_ini(WstVm *vm, const WstVmInstr *ins)
{

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

void vm_exec_inf(WstVm *vm, const WstVmInstr *ins)
{

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

void vm_exec_ld(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 8u;
    VM_REG_WRITE(vm, ins->rd, vm_mem_read64(vm, addr));
}

void vm_exec_lw(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 4u;
    uint32_t raw = vm_mem_read32(vm, addr);
    VM_REG_WRITE(vm, ins->rd, (U(64))(S(64))(S(32))raw);
}

void vm_exec_st(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 8u;
    vm_mem_write64(vm, addr, VM_REG_READ(vm, ins->rs2));
}

void vm_exec_lwu(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 4u;
    VM_REG_WRITE(vm, ins->rd, (uint64_t)vm_mem_read32(vm, addr));
}

void vm_exec_sw(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 4u;
    vm_mem_write32(vm, addr, (uint32_t)VM_REG_READ(vm, ins->rs2));
}

void vm_exec_fld(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 8u;
    vm->fp[ins->rd] = vm_fp_num(vm_mem_read64(vm, addr));
}

void vm_exec_fst(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 8u;
    vm_mem_write64(vm, addr, vm_fp_bits(vm->fp[ins->rs2]));
}

void vm_exec_pushqr(WstVm *vm, const WstVmInstr *ins)
{
    vm_stack_push64(vm, ins->addr, VM_REG_READ(vm, ins->rs1));
}

void vm_exec_pushqv(WstVm *vm, const WstVmInstr *ins)
{
    vm_stack_push64(vm, ins->addr, vm_fp_bits(vm->fp[ins->rs1]));
}

void vm_exec_popqr(WstVm *vm, const WstVmInstr *ins)
{
    VM_REG_WRITE(vm, ins->rd, vm_stack_pop64(vm, ins->addr));
}

void vm_exec_popqv(WstVm *vm, const WstVmInstr *ins)
{
    vm->fp[ins->rd] = vm_fp_num(vm_stack_pop64(vm, ins->addr));
}

void vm_exec_jal(WstVm *vm, const WstVmInstr *ins)
{
    VM_REG_WRITE(vm, ins->rd, (uint64_t)(ins->addr + WST_VM_INSTR_SIZE));
    vm->pc = (size_t)((int64_t)ins->addr + ins->imm);
}

void vm_exec_jalr(WstVm *vm, const WstVmInstr *ins)
{
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

/* Unsigned branches read the register bits directly. */
#define VM_DEF_BRANCH_U(name, cond) \
    void vm_exec_##name(WstVm *vm, const WstVmInstr *ins) \
    { \
        uint64_t lhs = VM_REG_READ(vm, ins->rs1); \
        uint64_t rhs = VM_REG_READ(vm, ins->rs2); \
        if (cond) \
            vm->pc = (size_t)((int64_t)ins->addr + ins->imm); \
    }

VM_DEF_BRANCH_U(bltu, lhs < rhs)
VM_DEF_BRANCH_U(bgeu, lhs >= rhs)

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
VM_DEF_R_INT(slt, ((S(64))lhs < (S(64))rhs) ? 1u : 0u)
VM_DEF_R_INT(sltu, lhs < rhs ? 1u : 0u)

VM_DEF_R_W(addw, lhs + rhs)
VM_DEF_R_W(subw, lhs - rhs)
VM_DEF_R_W(mulw, lhs * rhs)
VM_DEF_R_W(sllw, lhs << (rhs & 31u))
VM_DEF_R_W(srlw, lhs >> (rhs & 31u))
VM_DEF_R_W(sraw, (U(32))((S(32))lhs >> (rhs & 31u)))

void vm_exec_divw(WstVm *vm, const WstVmInstr *ins)
{
    uint32_t quot = 0, rem = 0;
    vm_divmodw((uint32_t)VM_REG_READ(vm, ins->rs1),
               (uint32_t)VM_REG_READ(vm, ins->rs2), &quot, &rem);
    VM_REG_WRITE(vm, ins->rd, vm_sext32(quot));
}

void vm_exec_divuw(WstVm *vm, const WstVmInstr *ins)
{
    uint32_t quot = 0, rem = 0;
    vm_divmoduw((uint32_t)VM_REG_READ(vm, ins->rs1),
                (uint32_t)VM_REG_READ(vm, ins->rs2), &quot, &rem);
    VM_REG_WRITE(vm, ins->rd, vm_sext32(quot));
}

void vm_exec_remw(WstVm *vm, const WstVmInstr *ins)
{
    uint32_t quot = 0, rem = 0;
    vm_divmodw((uint32_t)VM_REG_READ(vm, ins->rs1),
               (uint32_t)VM_REG_READ(vm, ins->rs2), &quot, &rem);
    VM_REG_WRITE(vm, ins->rd, vm_sext32(rem));
}

void vm_exec_remuw(WstVm *vm, const WstVmInstr *ins)
{
    uint32_t quot = 0, rem = 0;
    vm_divmoduw((uint32_t)VM_REG_READ(vm, ins->rs1),
                (uint32_t)VM_REG_READ(vm, ins->rs2), &quot, &rem);
    VM_REG_WRITE(vm, ins->rd, vm_sext32(rem));
}

void vm_exec_div(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t lhs = VM_REG_READ(vm, ins->rs1);
    uint64_t rhs = VM_REG_READ(vm, ins->rs2);
    uint64_t quot = 0, rem = 0;
    vm_divmod(lhs, rhs, &quot, &rem);
    VM_REG_WRITE(vm, ins->rd, quot);
}

void vm_exec_rem(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t lhs = VM_REG_READ(vm, ins->rs1);
    uint64_t rhs = VM_REG_READ(vm, ins->rs2);
    uint64_t quot = 0, rem = 0;
    vm_divmod(lhs, rhs, &quot, &rem);
    VM_REG_WRITE(vm, ins->rd, rem);
}

void vm_exec_addi(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t lhs = VM_REG_READ(vm, ins->rs1);
    VM_REG_WRITE(vm, ins->rd, lhs + (uint64_t)ins->imm);
}

void vm_exec_lui(WstVm *vm, const WstVmInstr *ins)
{
    VM_REG_WRITE(vm, ins->rd, (uint64_t)ins->imm);
}

void vm_exec_auipc(WstVm *vm, const WstVmInstr *ins)
{
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

VM_DEF_R_W_IMM(addiw, lhs + rhs)
VM_DEF_R_W_IMM(slliw, lhs << (rhs & 31u))
VM_DEF_R_W_IMM(srliw, lhs >> (rhs & 31u))
VM_DEF_R_W_IMM(sraiw, (U(32))((S(32))lhs >> (rhs & 31u)))

VM_DEF_R_FP(fadd, lhs + rhs)
VM_DEF_R_FP(fsub, lhs - rhs)
VM_DEF_R_FP(fmul, lhs * rhs)
VM_DEF_R_FP(fdiv, lhs / rhs)

void vm_exec_fsqrt(WstVm *vm, const WstVmInstr *ins)
{
    vm->fp[ins->rd] = sqrt(vm->fp[ins->rs1]);
}

VM_DEF_FP_CMP(feq, vm_fp_eq(lhs, rhs))
VM_DEF_FP_CMP(flt, lhs < rhs)
VM_DEF_FP_CMP(fle, lhs <= rhs)

void vm_exec_flw(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 4u;
    vm_f32_write(vm, ins->rd, vm_mem_read32(vm, addr));
}

void vm_exec_fsw(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t addr = VM_REG_READ(vm, ins->rs1) + (uint64_t)ins->imm * 4u;
    vm_mem_write32(vm, addr, (uint32_t)vm_fp_bits(vm->fp[ins->rs2]));
}

void vm_exec_fcvt_s_w(WstVm *vm, const WstVmInstr *ins)
{
    int32_t value = (int32_t)(uint32_t)VM_REG_READ(vm, ins->rs1);
    vm_f32_write(vm, ins->rd, vm_f32_bits((float)value));
}

void vm_exec_fcvt_w_s(WstVm *vm, const WstVmInstr *ins)
{
    float value = vm_f32_read(vm, ins->rs1);
    VM_REG_WRITE(vm, ins->rd, (uint64_t)(int64_t)vm_f32_to_i32(value));
}

VM_DEF_R_FP32(fadd_s, lhs + rhs)
VM_DEF_R_FP32(fsub_s, lhs - rhs)
VM_DEF_R_FP32(fmul_s, lhs * rhs)
VM_DEF_R_FP32(fdiv_s, lhs / rhs)
VM_DEF_R_FP32_1(fsqrt_s, sqrtf(src))
VM_DEF_FP32_CMP(feq_s, vm_fp_eq32(lhs, rhs))
VM_DEF_FP32_CMP(flt_s, lhs < rhs)
VM_DEF_FP32_CMP(fle_s, lhs <= rhs)
VM_DEF_R_FP32_1(fabs_s, fabsf(src))
VM_DEF_R_FP32_1(fneg_s, -src)
VM_DEF_R_FP32(fmax_s, fmaxf(lhs, rhs))
VM_DEF_R_FP32(fmin_s, fminf(lhs, rhs))

void vm_exec_fmv_s(WstVm *vm, const WstVmInstr *ins)
{
    vm_f32_write(vm, ins->rd, vm_f32_read_bits(vm, ins->rs1));
}

VM_DEF_R4_FP(fmadd, fma(a, b, c))
VM_DEF_R4_FP(fmsub, fma(a, b, -c))
VM_DEF_R4_FP(fnmadd, -fma(a, b, c))
VM_DEF_R4_FP(fnmsub, -fma(a, b, -c))

VM_DEF_R4_FP32(fmadd_s, fmaf(a, b, c))
VM_DEF_R4_FP32(fmsub_s, fmaf(a, b, -c))
VM_DEF_R4_FP32(fnmadd_s, -fmaf(a, b, c))
VM_DEF_R4_FP32(fnmsub_s, -fmaf(a, b, -c))

void vm_sys_exit(WstVm *vm, size_t addr)
{
    /* WST_VM_RUN_EXIT stops the host loop, the code stays in x2. */
    (void)vm;
    (void)addr;
}

void vm_sys_write(WstVm *vm, size_t addr)
{
    WstVmContext *ctx = vm->ctx;
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

void vm_sys_flush(WstVm *vm, size_t addr)
{
    (void)addr;

    ECALL_RET = 0;
}

void vm_exec_ecall(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t num = ECALL_NUM;

    /* out-of-range numbers fall out, NULL entries are unknown. */
    WstVmSyscallHandler handler =
        num < WST_VM_SYSCALL_COUNT ? vm_syscalls[(size_t)num] : NULL;
    if (handler == NULL) {
        LOG_E("unknown syscall %" PRIu64 " at pc 0x%08zx", num, ins->addr);
        exit(EXIT_FAILURE);
    }

    handler(vm, ins->addr);
}

void vm_exec_call(WstVm *vm, const WstVmInstr *ins)
{
    vm_stack_push64(vm, ins->addr,
                    (uint64_t)(ins->addr + WST_VM_INSTR_SIZE));
    vm->pc = (size_t)((int64_t)ins->addr + ins->imm);
}

void vm_exec_ret(WstVm *vm, const WstVmInstr *ins)
{
    uint64_t target = vm_stack_pop64(vm, ins->addr);
    if (target % WST_VM_INSTR_SIZE != 0) {
        LOG_E("misaligned ret target 0x%08" PRIx64 " at pc 0x%08zx",
              target, ins->addr);
        exit(EXIT_FAILURE);
    }
    vm->pc = (size_t)target;
}
