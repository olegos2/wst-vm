#ifndef VM_PRIV_H
#define VM_PRIV_H

#include "toys/vm.h"

#include <stddef.h>
#include <stdint.h>


/* Unsigned and signed integer types of the given width. */
#define U(sz) uint ## sz ## _t
#define S(sz) int ## sz ## _t

#define R_x0 (vm->regs[0])
#define R_x1 (vm->regs[1])
#define R_x2 (vm->regs[2])
#define R_x3 (vm->regs[3])
#define R_x4 (vm->regs[4])
#define R_x5 (vm->regs[5])
#define R_x6 (vm->regs[6])
#define R_x7 (vm->regs[7])
#define R_SP R_x7

#define ECALL_NUM  R_x1
#define ECALL_ARG0 R_x2
#define ECALL_ARG1 R_x3
#define ECALL_ARG2 R_x4
#define ECALL_RET  R_x1


/** Read guest memory, one accessor per operand size. */
uint64_t vm_mem_read64(WstVm *vm, uint64_t addr);
uint32_t vm_mem_read32(WstVm *vm, uint64_t addr);

/** Write guest memory, one accessor per operand size. */
void vm_mem_write64(WstVm *vm, uint64_t addr, uint64_t value);
void vm_mem_write32(WstVm *vm, uint64_t addr, uint32_t value);

/** Push 8 bytes to guest stack. */
void vm_stack_push64(WstVm *vm, size_t pc, uint64_t value);

/** Pop 8 bytes from guest stack. */
uint64_t vm_stack_pop64(WstVm *vm, size_t pc);

#define OP(mnemonic) void vm_exec_ ## mnemonic(WstVm *vm, const WstVmInstr *ins)

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
OP(jalr);
OP(beq);
OP(bne);
OP(blt);
OP(bge);
OP(add);
OP(sub);
OP(mul);
OP(div);
OP(rem);
OP(divu);
OP(remu);
OP(and);
OP(or);
OP(xor);
OP(sll);
OP(srl);
OP(sra);
OP(addi);
OP(andi);
OP(ori);
OP(xori);
OP(slli);
OP(srli);
OP(srai);
OP(slti);
OP(sltiu);
OP(auipc);
OP(lui);
OP(fadd);
OP(fsub);
OP(fmul);
OP(fdiv);
OP(fsqrt);
OP(feq);
OP(flt);
OP(fle);
OP(lwu);
OP(slt);
OP(sltu);
OP(bltu);
OP(bgeu);
OP(addw);
OP(subw);
OP(mulw);
OP(divw);
OP(divuw);
OP(remw);
OP(remuw);
OP(sllw);
OP(srlw);
OP(sraw);
OP(addiw);
OP(slliw);
OP(srliw);
OP(sraiw);
OP(flw);
OP(fsw);
OP(fcvt_s_w);
OP(fcvt_w_s);
OP(fadd_s);
OP(fsub_s);
OP(fmul_s);
OP(fdiv_s);
OP(fsqrt_s);
OP(feq_s);
OP(flt_s);
OP(fle_s);
OP(fabs_s);
OP(fneg_s);
OP(fmax_s);
OP(fmin_s);
OP(fmv_s);
OP(fmadd);
OP(fmsub);
OP(fnmadd);
OP(fnmsub);
OP(fmadd_s);
OP(fmsub_s);
OP(fnmadd_s);
OP(fnmsub_s);
OP(ecall);
OP(call);
OP(ret);

#undef OP

#define ECALL(name) void vm_sys_ ## name(WstVm *vm, size_t addr)

ECALL(exit);
ECALL(write);
ECALL(flush);

#undef ECALL

/* Syscall table, defined in table.c, dispatched by vm_exec_ecall. */
extern const WstVmSyscallHandler vm_syscalls[WST_VM_SYSCALL_COUNT];

#endif /* VM_PRIV_H */
