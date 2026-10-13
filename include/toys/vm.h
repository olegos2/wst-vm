#ifndef TOYS_VM_H
#define TOYS_VM_H

#include "toys/vec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/**
 * Default address space cap in bytes when `WstVmContext.mem_max` is zero.
 * Growth past the resolved cap is fatal, see `WstVmContext`.
 */
#define WST_VM_MEM_DEFAULT_MAX ((size_t)1024 * 1024 * 1024)

/**
 * Default stack size in bytes when `WstVmContext.stack_size` is zero.
 * Carved after the program image on load, see `WstVmContext`.
 */
#define WST_VM_STACK_DEFAULT_SIZE ((size_t)64 * 1024)

/**
 * Integer register holding the stack pointer, RISC-V `sp` convention.
 * `vm.regs[WST_VM_REG_SP]` grows down inside `[stack_base, stack_top]`.
 * Plain `ld`/`st` with `x2` as base give direct stack access with offsets.
 */
#define WST_VM_REG_SP (2)

#define WST_VM_OPCODE_BITS (7)
#define WST_VM_OPCODE_COUNT (1u << WST_VM_OPCODE_BITS)

/**
 * Instruction set, enum values match opcode bytes.
 */
typedef enum {
    WST_VM_HLT      = 0x00, /* halt with exit code */
    WST_VM_OUTI     = 0x01, /* print GPR as decimal */
    WST_VM_OUTF     = 0x02, /* print FP register as double */
    WST_VM_LD       = 0x03, /* load 64-bit, sign-extended */
    WST_VM_LW       = 0x04, /* load 32-bit, sign-extended */
    WST_VM_ST       = 0x05, /* store 64-bit */
    WST_VM_SW       = 0x06, /* store low 32 bits */
    WST_VM_FLD      = 0x07, /* load 64 bits into FP register */
    WST_VM_FST      = 0x08, /* store FP register as 64 bits */
    WST_VM_PUSHQR   = 0x09, /* push GPR bits */
    WST_VM_PUSHQV   = 0x0A, /* push FP register bits */
    WST_VM_POPQR    = 0x0B, /* pop bits into GPR */
    WST_VM_POPQV    = 0x0C, /* pop bits into FP register */
    WST_VM_JAL      = 0x0D, /* jump with link */
    WST_VM_BEQ      = 0x0E, /* branch if equal */
    WST_VM_BNE      = 0x0F, /* branch if not equal */
    WST_VM_BLT      = 0x10, /* branch if less, signed */
    WST_VM_BGE      = 0x11, /* branch if greater or equal, signed */
    WST_VM_ADD      = 0x12, /* add 2 GPRs, store res in 3rd GPR. */
    WST_VM_SUB      = 0x13, /* sub 2 GPRs, store res in 3rd GPR. */
    WST_VM_MUL      = 0x14,
    WST_VM_DIV      = 0x15,
    WST_VM_MOD      = 0x16,
    WST_VM_AND      = 0x17, /* bitwise AND on registers */
    WST_VM_OR       = 0x18, /* bitwise OR on registers */
    WST_VM_XOR      = 0x19, /* bitwise XOR on registers */
    WST_VM_SLL      = 0x1A, /* shift left unsigned */
    WST_VM_SRL      = 0x1B, /* shift right unsigned */
    WST_VM_SRA      = 0x1C, /* shift right signed */
    WST_VM_ADDI     = 0x1D, /* add register and immediate, store res in 3rd register. */
    WST_VM_LUI      = 0x1E,
    WST_VM_FADD     = 0x1F, /* add 2 FP regs, store res in 3rd FP reg. */
    WST_VM_FSUB     = 0x20, /* sub 2 FP regs, store res in 3rd FP reg. */
    WST_VM_FMUL     = 0x21,
    WST_VM_FDIV     = 0x22, 
    WST_VM_FSQRT    = 0x23,
    WST_VM_FPOW     = 0x24,
    WST_VM_FEQ      = 0x25, /* FP compare equal, 0/1 stored into GPR */
    WST_VM_FLT      = 0x26, /* FP compare less than, 0/1 stored into GPR */
    WST_VM_FLE      = 0x27, /* FP compare less then equal, 0/1 stored into GPR */
    WST_VM_ECALL    = 0x28, /* host call by number in imm */
    WST_VM_CALL     = 0x29, /* push pc+4 on stack, jump */
    WST_VM_RET      = 0x2A, /* pop pc from stack */
    WST_VM_INI      = 0x2B, /* read decimal integer from stdin into GPR */
    WST_VM_INF      = 0x2C, /* read double from stdin into FP register */
} WstVmOp;

/**
 * Host calls for `ecall`, number goes in imm.
 */
typedef enum {
    WST_VM_SYS_EXIT = 0,    /* exit with code from regs[1] */
    WST_VM_SYS_WRITE = 1,   /* write regs[3] bytes from regs[2] to fd regs[1] */
} WstVmSyscall;

/**
 * Instruction formats, fixed 4-byte words with 7-bit opcode.
 */
typedef enum {
    WST_VM_INSTR_FORMAT_INVALID = 0,
    WST_VM_INSTR_FORMAT_R,               /* opcode, rd, rs1, rs2 */
    WST_VM_INSTR_FORMAT_I,               /* opcode, rd, rs1, 19-bit signed imm */
    WST_VM_INSTR_FORMAT_S,               /* opcode, rs1, rs2, 19-bit signed imm */
    WST_VM_INSTR_FORMAT_B,               /* same shape as S, imm is PC-relative */
    WST_VM_INSTR_FORMAT_U,               /* opcode, rd, 20-bit imm shifted by 12 */
    WST_VM_INSTR_FORMAT_J,               /* opcode, rd, 22-bit signed PC-relative imm */
} WstVmInstrFormat;

/**
 * Operand slots an instruction uses, as a bitmask.
 */
typedef enum {
    WST_VM_OPND_NONE = 0,
    WST_VM_OPND_RD   = 1u << 0,
    WST_VM_OPND_RS1  = 1u << 1,
    WST_VM_OPND_RS2  = 1u << 2,
    WST_VM_OPND_IMM  = 1u << 3,
} WstVmOperand;

/**
 * Decoded instruction, immediates already sign-extended.
 */
typedef struct WstVmInstr {
    /** Instruction opcode. */
    WstVmOp op;
    /** Destination register index. */
    uint8_t rd;
    /** Source 1 register index. */
    uint8_t rs1;
    /** Source 2 register index. */
    uint8_t rs2;
    /** Immediate value. */
    int64_t imm;
    size_t addr;
} WstVmInstr;

/**
 * Thread state: registers, program counter, stack pointer.
 *
 * The stack lives in virtual memory, top to bottom inside the
 * `[stack_base, stack_top]` range owned by `WstVmContext`.
 * `regs[WST_VM_REG_SP]` points at the first free slot above the top
 * item (empty stack means `sp == stack_top`). Pushes decrement by 8,
 * pops increment. Misaligned `sp`, overflow past `stack_base` and
 * underflow past `stack_top` are fatal.
 */
typedef struct WstVm {
    uint64_t regs[8];
    double fp[8];
    size_t pc;
} WstVm;

/**
 * Vm context that manages virtual memory and saves entry point.
 *
 * `mem_max` caps `vaddr + memsz` on load and every load/store/fetch
 * past it is fatal. Zero means `WST_VM_MEM_DEFAULT_MAX`, set it
 * before `wst_vm_context_load` to override (example `vm --mem-max`).
 *
 * `stack_size` is carved after the program image on load, zero means
 * `WST_VM_STACK_DEFAULT_SIZE`. `stack_base`/`stack_top` are outputs:
 * after load set `vm.regs[WST_VM_REG_SP]` to `stack_top`
 * (the example runner does this next to `vm.pc = ctx.entry`).
 */
typedef struct WstVmContext {
    WstVecU8 mem;
    size_t entry;
    size_t mem_max;
    size_t stack_size;
    size_t stack_base;
    size_t stack_top;
} WstVmContext;

/**
 * Handler of a single instruction.
 *
 * Runs with live state, jumps overwrite pc.
 */
typedef void (*WstVmInstrHandler)(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins);

/**
 * Handler of a single syscall.
 */
typedef void (*WstVmSyscallHandler)(WstVm *vm, WstVmContext *ctx, size_t addr);


/**
 * One table row describes an opcode for the interpreter, the assembler
 * and the disassembler: text name, binary format, operand slots, behavior.
 *
 * `fregs` marks operand slots holding float registers, using
 * `WstVmOperand` bits (`RD`/`RS1`/`RS2`). Integer slots take `x0`-`x7`,
 * marked slots take `f0`-`f7`.
 */
typedef struct {
    const char *mnemonic;
    WstVmInstrFormat format;
    unsigned operands;
    unsigned fregs;
    WstVmInstrHandler handler;
} WstVmInstrDesc;

/**
 * Opcode table, indexed by opcode byte. Empty rows are invalid opcodes.
 */
extern const WstVmInstrDesc wst_vm_op_table[WST_VM_OPCODE_COUNT];

/**
 * Zero the state. Infallible, kept boolean for a stable API.
 *
 * @return `true` always.
 */
bool wst_vm_init(WstVm *vm);

/**
 * Load program file into a fresh address space, carving the stack
 * after the image. Honors `ctx->mem_max`/`ctx->stack_size`,
 * zero means default.
 *
 * @return `true` on success, `false` otherwise.
 */
bool wst_vm_context_load(WstVmContext *ctx, const char *path);

/**
 * Free the address space. Safe on zero state.
 */
void wst_vm_context_free(WstVmContext *ctx);

/**
 * Decode instruction at pc without moving it.
 *
 * @return `true` when instruction was decoded, `false` on error.
 */
bool wst_vm_decode(const WstVm *vm, const WstVmContext *ctx, WstVmInstr *ins);

/**
 * Execute one decoded instruction, moving pc past it.
 * Stack errors, bad memory and `hlt` are fatal.
 */
void wst_vm_execute(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins);

/**
 * Reset program counter. Safe on zero state.
 */
void wst_vm_close(WstVm *vm);

#endif /* TOYS_VM_H */
