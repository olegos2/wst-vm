#ifndef TOYS_VM_H
#define TOYS_VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef struct WstVmContext WstVmContext;


/** Default guest memory size. */
#define WST_VM_MEM_DEFAULT_SIZE ((size_t)256 * 1024 * 1024)

/** Default stack region size. */
#define WST_VM_STACK_DEFAULT_SIZE ((size_t)64 * 1024)

/** Largest framebuffer dimensions. */
#define WST_VM_FB_MAX_WIDTH 640
#define WST_VM_FB_MAX_HEIGHT 480

/** Config block magic, "WSTF", and version. */
#define WST_VM_CONFIG_MAGIC 0x57535446u
#define WST_VM_CONFIG_VERSION 1u

/** Stack pointer register. */
#define WST_VM_REG_SP 7

/** Instruction text buffer size. */
#define WST_VM_INSTR_TEXT_CAP 64

/** Instruction size in bytes. */
#define WST_VM_INSTR_SIZE 4

#define WST_VM_OPCODE_BITS 7
#define WST_VM_NUM_OPCODES (1u << WST_VM_OPCODE_BITS)

/**
 * Operand slots, an instruction takes at most this many operands.
 */
#define WST_VM_OPND_COUNT 5

#define WST_VM_NUM_GPR 8
#define WST_VM_NUM_FP  8

/**
 * Instruction set, enum values match opcode bytes.
 */
typedef enum {
    WST_VM_OUTI     = 0x01,
    WST_VM_OUTF     = 0x02,
    WST_VM_LD       = 0x03,
    WST_VM_LW       = 0x04,
    WST_VM_ST       = 0x05,
    WST_VM_SW       = 0x06,
    WST_VM_FLD      = 0x07,
    WST_VM_FST      = 0x08,
    WST_VM_PUSHQR   = 0x09,
    WST_VM_PUSHQV   = 0x0A,
    WST_VM_POPQR    = 0x0B,
    WST_VM_POPQV    = 0x0C,
    WST_VM_JAL      = 0x0D,
    WST_VM_BEQ      = 0x0E,
    WST_VM_BNE      = 0x0F,
    WST_VM_BLT      = 0x10,
    WST_VM_BGE      = 0x11,
    WST_VM_ADD      = 0x12,
    WST_VM_SUB      = 0x13,
    WST_VM_MUL      = 0x14,
    WST_VM_DIV      = 0x15,
    WST_VM_REM      = 0x16,
    WST_VM_AND      = 0x17,
    WST_VM_OR       = 0x18,
    WST_VM_XOR      = 0x19,
    WST_VM_SLL      = 0x1A,
    WST_VM_SRL      = 0x1B,
    WST_VM_SRA      = 0x1C,
    WST_VM_ADDI     = 0x1D,
    WST_VM_LUI      = 0x1E,
    WST_VM_FADD     = 0x1F,
    WST_VM_FSUB     = 0x20,
    WST_VM_FMUL     = 0x21,
    WST_VM_FDIV     = 0x22,
    WST_VM_FSQRT    = 0x23,
    WST_VM_FEQ      = 0x25,
    WST_VM_FLT      = 0x26,
    WST_VM_FLE      = 0x27,
    WST_VM_ECALL    = 0x28,
    WST_VM_CALL     = 0x29,
    WST_VM_RET      = 0x2A,
    WST_VM_INI      = 0x2B,
    WST_VM_INF      = 0x2C,
    WST_VM_JALR     = 0x2D,
    WST_VM_DIVU     = 0x2E,
    WST_VM_REMU     = 0x2F,
    WST_VM_ANDI     = 0x30,
    WST_VM_ORI      = 0x31,
    WST_VM_XORI     = 0x32,
    WST_VM_SLLI     = 0x33,
    WST_VM_SRLI     = 0x34,
    WST_VM_SRAI     = 0x35,
    WST_VM_SLTI     = 0x36,
    WST_VM_SLTIU    = 0x37,
    WST_VM_AUIPC    = 0x38,
    WST_VM_LWU      = 0x39,
    WST_VM_SLT      = 0x3A,
    WST_VM_SLTU     = 0x3B,
    WST_VM_BLTU     = 0x3C,
    WST_VM_BGEU     = 0x3D,
    WST_VM_ADDW     = 0x3E,
    WST_VM_SUBW     = 0x3F,
    WST_VM_MULW     = 0x40,
    WST_VM_DIVW     = 0x41,
    WST_VM_DIVUW    = 0x42,
    WST_VM_REMW     = 0x43,
    WST_VM_REMUW    = 0x44,
    WST_VM_SLLW     = 0x45,
    WST_VM_SRLW     = 0x46,
    WST_VM_SRAW     = 0x47,
    WST_VM_ADDIW    = 0x48,
    WST_VM_SLLIW    = 0x49,
    WST_VM_SRLIW    = 0x4A,
    WST_VM_SRAIW    = 0x4B,
    WST_VM_FLW      = 0x4C,
    WST_VM_FSW      = 0x4D,
    WST_VM_FCVT_S_W = 0x4E,
    WST_VM_FCVT_W_S = 0x4F,
    WST_VM_FADD_S   = 0x50,
    WST_VM_FSUB_S   = 0x51,
    WST_VM_FMUL_S   = 0x52,
    WST_VM_FDIV_S   = 0x53,
    WST_VM_FSQRT_S  = 0x54,
    WST_VM_FEQ_S    = 0x5B,
    WST_VM_FLT_S    = 0x5C,
    WST_VM_FLE_S    = 0x5D,
    WST_VM_FABS_S   = 0x5E,
    WST_VM_FNEG_S   = 0x5F,
    WST_VM_FMAX_S   = 0x60,
    WST_VM_FMIN_S   = 0x61,
    WST_VM_FMV_S    = 0x62,
    WST_VM_FMADD    = 0x63,
    WST_VM_FMSUB    = 0x64,
    WST_VM_FNMADD   = 0x65,
    WST_VM_FNMSUB   = 0x66,
    WST_VM_FMADD_S  = 0x67,
    WST_VM_FMSUB_S  = 0x68,
    WST_VM_FNMADD_S = 0x69,
    WST_VM_FNMSUB_S = 0x6A,
} WstVmOp;

/**
 * Host calls for `ecall`, compared against `x1`.
 */
typedef enum {
    WST_VM_SYS_EXIT  = 0,
    WST_VM_SYS_WRITE = 1,
    WST_VM_SYS_FLUSH = 2,
} WstVmSyscall;

/**
 * Syscall table size, some slots may be NULL.
 */
#define WST_VM_SYSCALL_COUNT 128

/**
 * Instruction formats, fixed 4-byte words with 7-bit opcode.
 */
typedef enum {
    WST_VM_INSTR_FORMAT_INVALID = 0,
    WST_VM_INSTR_FORMAT_R,
    WST_VM_INSTR_FORMAT_I,
    WST_VM_INSTR_FORMAT_S,
    WST_VM_INSTR_FORMAT_B,
    WST_VM_INSTR_FORMAT_U,
    WST_VM_INSTR_FORMAT_J,
    WST_VM_INSTR_FORMAT_R4,
    WST_VM_INSTR_FORMAT_COUNT,
} WstVmInstrFormat;

/**
 * Field layout of one instruction format.
 *
 * A zero shift means the format lacks the field.
 */
typedef struct {
    /** Register field shifts, zero when absent. */
    uint8_t rd_shift;
    uint8_t rs1_shift;
    uint8_t rs2_shift;
    uint8_t rs3_shift;
    /** Immediate shift and width. */
    uint8_t imm_shift;
    uint8_t imm_bits;
    /** Immediate is signed, unsigned ones store `imm >> imm_shift`. */
    bool imm_signed;
    /** Immediate counts from the instruction address. */
    bool pc_relative;
} WstVmInstrFormatDesc;

/**
 * Format table, indexed by `WstVmInstrFormat`.
 */
extern const WstVmInstrFormatDesc wst_vm_format_table[WST_VM_INSTR_FORMAT_COUNT];

/**
 * Operand slots an instruction uses, as a bitmask.
 * Bits ascend in assembly order (rd, rs1, rs2, rs3, imm).
 */
typedef enum {
    WST_VM_OPND_NONE = 0,
    WST_VM_OPND_RD   = 1u << 0,
    WST_VM_OPND_RS1  = 1u << 1,
    WST_VM_OPND_RS2  = 1u << 2,
    WST_VM_OPND_RS3  = 1u << 3,
    WST_VM_OPND_IMM  = 1u << 4,
} WstVmOperand;

/**
 * Decoded instruction, immediates already sign-extended.
 */
typedef struct WstVmInstr {
    WstVmOp op;
    /** Register indices. */
    uint8_t rd, rs1, rs2, rs3;
    /** Immediate and instruction address. */
    int64_t imm;
    size_t addr;
} WstVmInstr;

/**
 * Per-thread vm state.
 */
typedef struct WstVm {
    uint64_t regs[WST_VM_NUM_GPR];
    /** FP registers hold raw bits, f32 values are NaN-boxed. */
    double fp[WST_VM_NUM_FP];
    size_t pc;
    /** Context bound by `wst_vm_init`, must outlive vm. */
    WstVmContext *ctx;
} WstVm;

/**
 * Result of interpreting instruction.
 */
typedef enum {
    /** Instruction finished, keep running. */
    WST_VM_RUN_CONTINUE = 0,
    /** Guest asked the host for a frame. */
    WST_VM_RUN_FLUSH,
    /** Guest stopped with the `exit` host call. */
    WST_VM_RUN_EXIT,
    /** Host could not decode the next instruction. */
    WST_VM_RUN_ERROR,
} WstVmRet;

/**
 * Value stack at the top of guest RAM, growing down.
 * `size` is the requested size, `base`/`top` are set on load.
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
    WST_VM_FB_FORMAT_RGBA8888 = 1,
} WstVmFbFormat;

/**
 * Firmware configuration copied into guest memory on load.
 * Field offsets and pixel formats are in docs/memory-map.md.
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
    /** Whole guest memory size in bytes. */
    uint64_t mem_size;
    /** First byte above the stack. */
    uint64_t stack_top;
} WstVmConfig;

/**
 * Vm context that holds guest environment (mem, stack, config).
 */
struct WstVmContext {
    /** Guest RAM, reserved on load. */
    uint8_t *mem;
    /** Guest RAM size, request on input and reservation on success. */
    size_t mem_size;
    /** Program entry. */
    size_t entry;
    /** Config passed to guest. */
    WstVmConfig config;
    /** Address where config was placed for guest. */
    size_t config_addr;
    WstVmStack stack;
};

/**
 * Handler of a single instruction.
 *
 * Runs with live state, jumps overwrite pc.
 */
typedef void (*WstVmInstrHandler)(WstVm *vm, const WstVmInstr *ins);

/**
 * Handler of a single syscall.
 */
typedef void (*WstVmSyscallHandler)(WstVm *vm, size_t addr);


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
 * Reset vm registers, pc and context pointer.
 */
void wst_vm_init(WstVm *vm, WstVmContext *ctx);

/**
 * Load a program file and lay out guest RAM, stack, config and
 * framebuffer. `ctx->mem_size` and `ctx->stack.size` are requests,
 * zero means the default.
 *
 * @return `true` on success, `false` otherwise.
 */
bool wst_vm_context_load(WstVmContext *ctx, const char *path);

/**
 * Free the address space. Safe on zero state.
 */
void wst_vm_context_free(WstVmContext *ctx);

/**
 * Decode instruction at pc, does not advance pc.
 *
 * @return `true` when instruction was decoded, `false` on error.
 */
bool wst_vm_decode(const WstVm *vm, WstVmInstr *ins);

/**
 * Decode one instruction word, `addr` goes to `ins->addr`.
 *
 * @return `true` when word was decoded, `false` on unknown opcode.
 */
bool wst_vm_decode_word(uint32_t word, size_t addr, WstVmInstr *ins);

/**
 * Write one decoded instruction assembly line into `buf`.
 * `imm_text` is used to insert labels in place of imm.
 *
 * @return `true` on success, `false` when `buf` is too small.
 */
bool wst_vm_instr_text(const WstVmInstr *ins, const char *imm_text,
                       char *buf, size_t cap);

/**
 * Execute one decoded instruction and advance pc.
 * Stack and memory errors are fatal. The host resumes after a flush
 * and stops after an exit.
 *
 * @return requested program continuation.
 */
WstVmRet wst_vm_execute(WstVm *vm, const WstVmInstr *ins);

/**
 * Reset pc and the context pointer. Safe on zero state.
 */
void wst_vm_close(WstVm *vm);

#endif /* TOYS_VM_H */
