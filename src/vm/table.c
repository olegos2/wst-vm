#include "toys/vm.h"
#include "vm_priv.h"

#include <stddef.h>


const WstVmSyscallHandler vm_syscalls[WST_VM_SYSCALL_COUNT] = {
    [WST_VM_SYS_EXIT] = vm_sys_exit,
    [WST_VM_SYS_WRITE] = vm_sys_write,
};

const WstVmInstrDesc wst_vm_op_table[WST_VM_OPCODE_COUNT] = {
    [WST_VM_HLT]      = { "hlt",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_hlt },

    [WST_VM_OUTI]     = { "outi",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RS1,
                          WST_VM_OPND_NONE, vm_exec_outi },

    [WST_VM_OUTF]     = { "outf",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RS1,
                          WST_VM_OPND_RS1, vm_exec_outf },

    [WST_VM_INI]      = { "ini",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD,
                          WST_VM_OPND_NONE, vm_exec_ini },

    [WST_VM_INF]      = { "inf",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD,
                          WST_VM_OPND_RD, vm_exec_inf },

    [WST_VM_LD]       = { "ld",       WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_ld },

    [WST_VM_LW]       = { "lw",       WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_lw },

    [WST_VM_ST]       = { "st",       WST_VM_INSTR_FORMAT_S,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_st },

    [WST_VM_SW]       = { "sw",       WST_VM_INSTR_FORMAT_S,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_sw },

    [WST_VM_FLD]      = { "fld",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_RD, vm_exec_fld },

    [WST_VM_FST]      = { "fst",      WST_VM_INSTR_FORMAT_S,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_RS2, vm_exec_fst },

    [WST_VM_PUSHQR]   = { "pushqr",   WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RS1,
                          WST_VM_OPND_NONE, vm_exec_pushqr },

    [WST_VM_PUSHQV]   = { "pushqv",   WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RS1,
                          WST_VM_OPND_RS1, vm_exec_pushqv },

    [WST_VM_POPQR]    = { "popqr",    WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD,
                          WST_VM_OPND_NONE, vm_exec_popqr },

    [WST_VM_POPQV]    = { "popqv",    WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD,
                          WST_VM_OPND_RD, vm_exec_popqv },

    [WST_VM_JAL]      = { "jal",      WST_VM_INSTR_FORMAT_J,
                          WST_VM_OPND_RD  | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_jal },

    [WST_VM_BEQ]      = { "beq",      WST_VM_INSTR_FORMAT_B,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_beq },

    [WST_VM_BNE]      = { "bne",      WST_VM_INSTR_FORMAT_B,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_bne },

    [WST_VM_BLT]      = { "blt",      WST_VM_INSTR_FORMAT_B,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_blt },

    [WST_VM_BGE]      = { "bge",      WST_VM_INSTR_FORMAT_B,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_bge },

    [WST_VM_ADD]      = { "add",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_add },

    [WST_VM_SUB]      = { "sub",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_sub },

    [WST_VM_MUL]      = { "mul",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_mul },

    [WST_VM_DIV]      = { "div",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_div },

    [WST_VM_MOD]      = { "mod",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_mod },

    [WST_VM_AND]      = { "and",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_and },

    [WST_VM_OR]       = { "or",       WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_or },

    [WST_VM_XOR]      = { "xor",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_xor },

    [WST_VM_SLL]      = { "sll",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_sll },

    [WST_VM_SRL]      = { "srl",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_srl },

    [WST_VM_SRA]      = { "sra",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_sra },

    [WST_VM_ADDI]     = { "addi",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_addi },

    [WST_VM_LUI]      = { "lui",      WST_VM_INSTR_FORMAT_U,
                          WST_VM_OPND_RD  | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_lui },

    [WST_VM_FADD]     = { "fadd",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fadd },

    [WST_VM_FSUB]     = { "fsub",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fsub },

    [WST_VM_FMUL]     = { "fmul",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fmul },

    [WST_VM_FDIV]     = { "fdiv",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fdiv },

    [WST_VM_FSQRT]    = { "fsqrt",    WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1, vm_exec_fsqrt },

    [WST_VM_FEQ]      = { "feq",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_feq },

    [WST_VM_FLT]      = { "flt",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_flt },

    [WST_VM_FLE]      = { "fle",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fle },

    [WST_VM_ECALL]    = { "ecall",    WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_NONE,
                          WST_VM_OPND_NONE, vm_exec_ecall },

    [WST_VM_CALL]     = { "call",     WST_VM_INSTR_FORMAT_J,
                          WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_call },

    [WST_VM_RET]      = { "ret",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_NONE,
                          WST_VM_OPND_NONE, vm_exec_ret },
};
