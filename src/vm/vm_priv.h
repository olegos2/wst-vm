#ifndef VM_PRIV_H
#define VM_PRIV_H

#include "toys/vm.h"

#include <stddef.h>
#include <stdint.h>


/** Read guest memory address of up to 8 bytes. */
uint64_t vm_mem_read(WstVmContext *ctx, uint64_t addr, unsigned width);

/** Write guest memory up to 8 bytes. */
void vm_mem_write(WstVmContext *ctx, uint64_t addr, uint64_t value, unsigned width);

/** Push 8 bytes to guest stack. */
void vm_stack_push(WstVm *vm, WstVmContext *ctx, size_t pc, uint64_t value);

/** Pop 8 bytes from guest stack. */
uint64_t vm_stack_pop(WstVm *vm, WstVmContext *ctx, size_t pc);

#define OP(mnemonic) void vm_exec_ ## mnemonic(WstVm *vm, WstVmContext *ctx, const WstVmInstr *ins)

OP(hlt);
OP(outi);
OP(outf);
OP(ini);
OP(inf);
OP(ld);
OP(lw);
OP(st);
OP(sw);
OP(fld);
OP(fst);
OP(pushqr);
OP(pushqv);
OP(popqr);
OP(popqv);
OP(jal);
OP(beq);
OP(bne);
OP(blt);
OP(bge);
OP(add);
OP(sub);
OP(mul);
OP(div);
OP(mod);
OP(and);
OP(or);
OP(xor);
OP(sll);
OP(srl);
OP(sra);
OP(addi);
OP(lui);
OP(fadd);
OP(fsub);
OP(fmul);
OP(fdiv);
OP(fsqrt);
OP(fpow);
OP(feq);
OP(flt);
OP(fle);
OP(ecall);
OP(call);
OP(ret);

#undef OP

#define ECALL(name) void vm_sys_ ## name(WstVm *vm, WstVmContext *ctx, size_t addr)

ECALL(exit);
ECALL(write);

#undef ECALL

/* Syscall table, defined in table.c, dispatched by vm_exec_ecall. */
extern const WstVmSyscallHandler vm_syscalls[];
extern const size_t vm_syscall_count;

#endif /* VM_PRIV_H */
