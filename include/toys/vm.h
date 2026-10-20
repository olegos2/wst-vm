#ifndef TOYS_VM_H
#define TOYS_VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/**
 * Default memory size in bytes, reserved once on load.
 */
#define WST_VM_MEM_DEFAULT_MAX ((size_t)256 * 1024 * 1024)

/**
 * Default stack region size at the top of guest memory.
 */
#define WST_VM_STACK_DEFAULT_SIZE ((size_t)64 * 1024)

/**
 * Largest framebuffer dimensions the vm places.
 */
#define WST_VM_FB_MAX_WIDTH 640
#define WST_VM_FB_MAX_HEIGHT 480

/**
 * Firmware configuration magic, "WSTF".
 */
#define WST_VM_CONFIG_MAGIC 0x57535446u

/**
 * Firmware configuration version.
 */
#define WST_VM_CONFIG_VERSION 1u

/**
 * Stack pointer register. Roles: `x0` is zero, `x1`-`x3` take params
 * with the return value in `x1`, `x4`-`x5` are callee-saved, `x6` is
 * the link register and caller-saved with the params, `x7` is sp.
 * `vm.regs[WST_VM_REG_SP]` grows down inside the `WstVmStack` range.
 */
#define WST_VM_REG_SP 7

/**
 * Buffer size that always holds instruction text.
 */
#define WST_VM_INSTR_TEXT_CAP 64

/**
 * Instruction size in bytes, pcs and code addresses align to it.
 */
#define WST_VM_INSTR_SIZE 4

#define WST_VM_OPCODE_BITS 7
#define WST_VM_NUM_OPCODES (1u << WST_VM_OPCODE_BITS)

/**
 * Operand slots, an instruction takes at most this many operands.
 */
#define WST_VM_OPND_COUNT 4

#define WST_VM_NUM_GPR 8
#define WST_VM_NUM_FP  8

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
    WST_VM_REM      = 0x16, /* signed remainder, RISC-V REM */
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
    /* 0x24 retired */
    WST_VM_FEQ      = 0x25, /* FP compare equal, 0/1 stored into GPR */
    WST_VM_FLT      = 0x26, /* FP compare less than, 0/1 stored into GPR */
    WST_VM_FLE      = 0x27, /* FP compare less then equal, 0/1 stored into GPR */
    WST_VM_ECALL    = 0x28, /* host call by number in x1 */
    WST_VM_CALL     = 0x29, /* push pc+4 on stack, jump */
    WST_VM_RET      = 0x2A, /* pop pc from stack */
    WST_VM_INI      = 0x2B, /* read decimal integer from stdin into GPR */
    WST_VM_INF      = 0x2C, /* read double from stdin into FP register */
    WST_VM_JALR     = 0x2D, /* jump to register with link */
    WST_VM_DIVU     = 0x2E, /* unsigned divide */
    WST_VM_REMU     = 0x2F, /* unsigned remainder */
    WST_VM_ANDI     = 0x30, /* bitwise AND with immediate */
    WST_VM_ORI      = 0x31, /* bitwise OR with immediate */
    WST_VM_XORI     = 0x32, /* bitwise XOR with immediate */
    WST_VM_SLLI     = 0x33, /* shift left logical with immediate */
    WST_VM_SRLI     = 0x34, /* shift right logical with immediate */
    WST_VM_SRAI     = 0x35, /* shift right arithmetic with immediate */
    WST_VM_SLTI     = 0x36, /* set if less than immediate, signed */
    WST_VM_SLTIU    = 0x37, /* set if less than immediate, unsigned */
    WST_VM_AUIPC    = 0x38, /* add upper immediate to pc */
} WstVmOp;

/**
 * Host calls for `ecall`, number goes in `x1`.
 */
typedef enum {
    WST_VM_SYS_EXIT  = 0,   /* exit with code from x2 */
    WST_VM_SYS_WRITE = 1,   /* write x4 bytes from x3 to fd x2, count back in x1 */
    WST_VM_SYS_FLUSH = 2,   /* yield for a host frame, 0 in x1 */
} WstVmSyscall;

/**
 * Syscall table size, entries past the last named call stay NULL.
 */
#define WST_VM_SYSCALL_COUNT 128

/**
 * Instruction formats, fixed 4-byte words with 7-bit opcode.
 */
typedef enum {
    WST_VM_INSTR_FORMAT_INVALID = 0,
    WST_VM_INSTR_FORMAT_R,  /* rd, rs1, rs2 */
    WST_VM_INSTR_FORMAT_I,  /* rd, rs1, imm */
    WST_VM_INSTR_FORMAT_S,  /* rs1, rs2, imm */
    WST_VM_INSTR_FORMAT_B,  /* like S, imm counts from pc */
    WST_VM_INSTR_FORMAT_U,  /* rd, imm scaled to the high bits */
    WST_VM_INSTR_FORMAT_J,  /* rd, imm counts from pc */
    WST_VM_INSTR_FORMAT_COUNT,
} WstVmInstrFormat;

/**
 * Field layout of one instruction format.
 *
 * A zero shift means the format lacks the field, the opcode owns
 * bits 6:0. Immediate fields sit in the high bits and signed ones
 * sign-extend back from `imm_bits`.
 */
typedef struct {
    /** Bit shifts of the register fields. */
    uint8_t rd_shift;
    uint8_t rs1_shift;
    uint8_t rs2_shift;
    /** Bit shift and width of the immediate field. */
    uint8_t imm_shift;
    uint8_t imm_bits;
    /** Immediate is signed, unsigned ones store `imm >> imm_shift`. */
    bool imm_signed;
    /** Immediate counts from the instruction address, not zero. */
    bool pc_relative;
} WstVmInstrFormatDesc;

/**
 * Format table, indexed by `WstVmInstrFormat`.
 */
extern const WstVmInstrFormatDesc wst_vm_format_table[WST_VM_INSTR_FORMAT_COUNT];

/**
 * Operand slots an instruction uses, as a bitmask.
 * Bits ascend in assembly order (rd, rs1, rs2, imm).
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
    /** Instruction address. */
    size_t addr;
} WstVmInstr;

/**
 * Per-thread vm state.
 */
typedef struct WstVm {
    /** General purpose registers */
    uint64_t regs[WST_VM_NUM_GPR];
    /** FP registers */
    double fp[WST_VM_NUM_FP];
    /** Program counter */
    size_t pc;
} WstVm;

/**
 * Result of one execution step.
 */
typedef enum {
    /** Instruction finished, keep running. */
    WST_VM_RUN_CONTINUE = 0,
    /** Guest asked the host for a frame with `flush`. */
    WST_VM_RUN_FLUSH,
} WstVmRet;

/**
 * Value stack at the top of guest RAM, growing down.
 *
 * `size` is taken on load, zero means `WST_VM_STACK_DEFAULT_SIZE`.
 * `base`/`top` are outputs set by `wst_vm_context_load`.
 *
 * The range lives in shared memory so stack slots stay directly
 * addressable (`ld`/`st` with `x7`), the pointer itself is per-thread
 * state in `WstVm.regs[WST_VM_REG_SP]`.
 */
typedef struct {
    size_t size;
    size_t base;
    size_t top;
} WstVmStack;

/**
 * Guest pixel formats.
 */
typedef enum {
    /** Four bytes per pixel in R, G, B, A order, raylib RGBA8. */
    WST_VM_FB_FORMAT_RGBA8888 = 1,
} WstVmFbFormat;

/**
 * Firmware configuration copied into guest memory on load.
 *
 * The guest gets its address in `x1` at entry. `fb_width` and
 * `fb_height` are zero when the framebuffer is off, `fb_addr` points
 * at the first pixel and `fb_stride` is the byte distance between
 * rows. `mem_size` covers the whole reservation, `stack_top` is the
 * first byte above the stack.
 */
typedef struct {
    /** `WST_VM_CONFIG_MAGIC`. */
    uint32_t magic;
    /** `WST_VM_CONFIG_VERSION`. */
    uint32_t version;
    /** Framebuffer width in pixels. */
    uint32_t fb_width;
    /** Framebuffer height in pixels. */
    uint32_t fb_height;
    /** Guest address of the first pixel. */
    uint64_t fb_addr;
    /** Bytes per framebuffer row. */
    uint32_t fb_stride;
    /** Pixel format, a `WstVmFbFormat`. */
    uint32_t fb_format;
    /** Whole guest memory reservation in bytes. */
    uint64_t mem_size;
    /** First byte above the stack. */
    uint64_t stack_top;
} WstVmConfig;

/**
 * Vm context that owns guest physical memory.
 *
 * `mem` is reserved once on load with `calloc`, never expanded.
 * `mem_size` is the reserved size. `mem_max` caps it, zero means
 * `WST_VM_MEM_DEFAULT_MAX`, set it before `wst_vm_context_load`
 * to override. Every load/store/fetch past the reservation is fatal.
 *
 * `config` describes the firmware handoff. Set `config.fb_width`,
 * `config.fb_height` and `config.fb_format` before load to place a
 * framebuffer, zero width and height keep it off. Load fills the
 * remaining fields, copies the struct into guest memory at
 * `config_addr` and places it below the framebuffer.
 *
 * After load set `pc` to `entry`, sp to `stack.top` and `x1` to
 * `config_addr`.
 */
typedef struct WstVmContext {
    uint8_t *mem;
    size_t mem_size;
    size_t entry;
    size_t mem_max;
    WstVmConfig config;
    size_t config_addr;
    WstVmStack stack;
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
 * Unified instruction description and its handler.
 */
typedef struct {
    const char *mnemonic;
    /** Instruction encoding format. */
    WstVmInstrFormat format;
    /** Instruction operands bit mask. */
    unsigned operands;
    /** Mask of operands that are fp registers instead of GPRs. */
    unsigned fregs;
    WstVmInstrHandler handler;
    /** Immediate scaling in access-size units, 1 for non-memory ops. */
    unsigned imm_scale;
} WstVmInstrDesc;

/**
 * Opcode table, indexed by opcode byte. Empty rows are invalid opcodes.
 */
extern const WstVmInstrDesc wst_vm_op_table[WST_VM_NUM_OPCODES];

/**
 * Zero the state. Infallible, kept boolean for a stable API.
 *
 * @return `true` always.
 */
bool wst_vm_init(WstVm *vm);

/**
 * Load program file into a fresh address space with the stack
 * on top of RAM and the firmware config below it.
 *
 * Honors `mem_max`, `stack.size` and the framebuffer request in
 * `ctx->config`, then fills `config_addr`, `config` and the stack
 * range. The guest should start with `x1` set to `config_addr`.
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
 * Decode one instruction word, `addr` goes to `ins->addr`.
 *
 * @return `true` when word was decoded, `false` on unknown opcode.
 */
bool wst_vm_decode_word(uint32_t word, size_t addr, WstVmInstr *ins);

/**
 * Write one decoded instruction as `mnemonic operands` into `buf`.
 * `imm_text` prints in place of the immediate when not NULL.
 * Output can be re-assembled. `WST_VM_INSTR_TEXT_CAP` always fits.
 *
 * @return `true` on success, `false` when `buf` is too small.
 */
bool wst_vm_instr_text(const WstVmInstr *ins, const char *imm_text,
                       char *buf, size_t cap);

/**
 * Execute one decoded instruction, moving pc past it.
 * Stack errors, bad memory and `hlt` are fatal.
 *
 * @return request to caller on how to continue execution.
 */
WstVmRet wst_vm_execute(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins);

/**
 * Reset program counter. Safe on zero state.
 */
void wst_vm_close(WstVm *vm);

#endif /* TOYS_VM_H */
