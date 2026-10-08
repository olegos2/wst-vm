#ifndef TOYS_VM_H
#define TOYS_VM_H

#include "toys/file.h"
#include "toys/stk.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * Instruction set, enum values match opcode bytes.
 */
typedef enum {
    WST_VM_HLT = 0x00,     /* hlt */
    WST_VM_PUSHQ = 0x01,   /* pushq imm64 */
    WST_VM_POPQ = 0x02,    /* popq */
    WST_VM_ADDSD = 0x03,   /* addsd */
    WST_VM_SUBSD = 0x04,   /* subsd */
    WST_VM_MULSD = 0x05,   /* mulsd */
    WST_VM_DIVSD = 0x06,   /* divsd */
    WST_VM_POWSD = 0x07,   /* powsd */
    WST_VM_OUTD = 0x08,    /* outd */
} WstVmOp;

/**
 * Decoded instruction.
 */
typedef struct {
    WstVmOp op;
    double imm;
} WstVmInstr;

/**
 * Stack machine state, owns program mapping and value stack.
 */
typedef struct {
    WstFile prog;
    size_t rip;
    WstStkDouble stack;
} WstVm;

/**
 * Zero the state and init the value stack.
 *
 * @return `true` on success, `false` otherwise
 */
bool wst_vm_init(WstVm *vm);

/**
 * Load program file into memory, set rip to first instruction.
 * Unloads previous program, if any.
 *
 * @return `true` on success, `false` otherwise.
 */
bool wst_vm_open(WstVm *vm, const char *path);

/**
 * Decode instruction at rip, advance rip past it.
 *
 * @return `true` when instruction was decoded, `false` on error.
 */
bool wst_vm_decode(WstVm *vm, WstVmInstr *instr);

/**
 * Execute one decoded instruction.
 * Stack errors are fatal, hlt exits the process.
 */
void wst_vm_execute(WstVm *vm, const WstVmInstr *instr);

/**
 * Free value stack and unload program. Safe on zero state.
 */
void wst_vm_close(WstVm *vm);

#endif /* TOYS_VM_H */
