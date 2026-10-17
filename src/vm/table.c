#include "toys/vm.h"
#include "vm_priv.h"

#include <stddef.h>


const WstVmSyscallHandler vm_syscalls[WST_VM_SYSCALL_COUNT] = {
    [WST_VM_SYS_EXIT] = vm_sys_exit,
    [WST_VM_SYS_WRITE] = vm_sys_write,
};

const WstVmInstrDesc wst_vm_op_table[WST_VM_NUM_OPCODES] = {
    [WST_VM_HLT]      = { "hlt",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_hlt, 1 },

    [WST_VM_OUTI]     = { "outi",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RS1,
                          WST_VM_OPND_NONE, vm_exec_outi, 1 },

    [WST_VM_OUTF]     = { "outf",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RS1,
                          WST_VM_OPND_RS1, vm_exec_outf, 1 },

    [WST_VM_INI]      = { "ini",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD,
                          WST_VM_OPND_NONE, vm_exec_ini, 1 },

    [WST_VM_INF]      = { "inf",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD,
                          WST_VM_OPND_RD, vm_exec_inf, 1 },

    [WST_VM_LD]       = { "ld",       WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_ld, 8 },

    [WST_VM_LW]       = { "lw",       WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_lw, 4 },

    [WST_VM_ST]       = { "st",       WST_VM_INSTR_FORMAT_S,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_st, 8 },

    [WST_VM_SW]       = { "sw",       WST_VM_INSTR_FORMAT_S,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_sw, 4 },

    [WST_VM_FLD]      = { "fld",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_RD, vm_exec_fld, 8 },

    [WST_VM_FST]      = { "fst",      WST_VM_INSTR_FORMAT_S,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_RS2, vm_exec_fst, 8 },

    [WST_VM_PUSHQR]   = { "pushqr",   WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RS1,
                          WST_VM_OPND_NONE, vm_exec_pushqr, 1 },

    [WST_VM_PUSHQV]   = { "pushqv",   WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RS1,
                          WST_VM_OPND_RS1, vm_exec_pushqv, 1 },

    [WST_VM_POPQR]    = { "popqr",    WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD,
                          WST_VM_OPND_NONE, vm_exec_popqr, 1 },

    [WST_VM_POPQV]    = { "popqv",    WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD,
                          WST_VM_OPND_RD, vm_exec_popqv, 1 },

    [WST_VM_JAL]      = { "jal",      WST_VM_INSTR_FORMAT_J,
                          WST_VM_OPND_RD  | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_jal, 1 },

    [WST_VM_JALR]     = { "jalr",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_jalr, 1 },

    [WST_VM_BEQ]      = { "beq",      WST_VM_INSTR_FORMAT_B,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_beq, 1 },

    [WST_VM_BNE]      = { "bne",      WST_VM_INSTR_FORMAT_B,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_bne, 1 },

    [WST_VM_BLT]      = { "blt",      WST_VM_INSTR_FORMAT_B,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_blt, 1 },

    [WST_VM_BGE]      = { "bge",      WST_VM_INSTR_FORMAT_B,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_bge, 1 },

    [WST_VM_ADD]      = { "add",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_add, 1 },

    [WST_VM_SUB]      = { "sub",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_sub, 1 },

    [WST_VM_MUL]      = { "mul",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_mul, 1 },

    [WST_VM_DIV]      = { "div",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_div, 1 },

    [WST_VM_REM]      = { "rem",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_rem, 1 },

    [WST_VM_DIVU]     = { "divu",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_divu, 1 },

    [WST_VM_REMU]     = { "remu",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_remu, 1 },

    [WST_VM_AND]      = { "and",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_and, 1 },

    [WST_VM_OR]       = { "or",       WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_or, 1 },

    [WST_VM_XOR]      = { "xor",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_xor, 1 },

    [WST_VM_SLL]      = { "sll",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_sll, 1 },

    [WST_VM_SRL]      = { "srl",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_srl, 1 },

    [WST_VM_SRA]      = { "sra",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_sra, 1 },

    [WST_VM_ADDI]     = { "addi",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_addi, 1 },

    [WST_VM_ANDI]     = { "andi",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_andi, 1 },

    [WST_VM_ORI]      = { "ori",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_ori, 1 },

    [WST_VM_XORI]     = { "xori",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_xori, 1 },

    [WST_VM_SLLI]     = { "slli",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_slli, 1 },

    [WST_VM_SRLI]     = { "srli",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_srli, 1 },

    [WST_VM_SRAI]     = { "srai",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_srai, 1 },

    [WST_VM_SLTI]     = { "slti",     WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_slti, 1 },

    [WST_VM_SLTIU]    = { "sltiu",    WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_sltiu, 1 },

    [WST_VM_LUI]      = { "lui",      WST_VM_INSTR_FORMAT_U,
                          WST_VM_OPND_RD  | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_lui, 1 },

    [WST_VM_AUIPC]    = { "auipc",    WST_VM_INSTR_FORMAT_U,
                          WST_VM_OPND_RD  | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_auipc, 1 },

    [WST_VM_FADD]     = { "fadd",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fadd, 1 },

    [WST_VM_FSUB]     = { "fsub",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fsub, 1 },

    [WST_VM_FMUL]     = { "fmul",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fmul, 1 },

    [WST_VM_FDIV]     = { "fdiv",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fdiv, 1 },

    [WST_VM_FSQRT]    = { "fsqrt",    WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1, vm_exec_fsqrt, 1 },

    [WST_VM_FEQ]      = { "feq",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_feq, 1 },

    [WST_VM_FLT]      = { "flt",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_flt, 1 },

    [WST_VM_FLE]      = { "fle",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fle, 1 },

    [WST_VM_ECALL]    = { "ecall",    WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_NONE,
                          WST_VM_OPND_NONE, vm_exec_ecall, 1 },

    [WST_VM_CALL]     = { "call",     WST_VM_INSTR_FORMAT_J,
                          WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_call, 1 },

    [WST_VM_RET]      = { "ret",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_NONE,
                          WST_VM_OPND_NONE, vm_exec_ret, 1 },
};
