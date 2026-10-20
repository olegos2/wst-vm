#include "toys/vm.h"
#include "vm_priv.h"

#include <stddef.h>


const WstVmSyscallHandler vm_syscalls[WST_VM_SYSCALL_COUNT] = {
    [WST_VM_SYS_EXIT]  = vm_sys_exit,
    [WST_VM_SYS_WRITE] = vm_sys_write,
    [WST_VM_SYS_FLUSH] = vm_sys_flush,
};

const WstVmInstrFormatDesc wst_vm_format_table[WST_VM_INSTR_FORMAT_COUNT] = {
    [WST_VM_INSTR_FORMAT_R] = {
        .rd_shift = 7, .rs1_shift = 10, .rs2_shift = 13,
    },
    [WST_VM_INSTR_FORMAT_I] = {
        .rd_shift = 7, .rs1_shift = 10,
        .imm_shift = 13, .imm_bits = 19, .imm_signed = true,
    },
    [WST_VM_INSTR_FORMAT_S] = {
        .rs1_shift = 7, .rs2_shift = 10,
        .imm_shift = 13, .imm_bits = 19, .imm_signed = true,
    },
    [WST_VM_INSTR_FORMAT_B] = {
        .rs1_shift = 7, .rs2_shift = 10,
        .imm_shift = 13, .imm_bits = 19, .imm_signed = true,
        .pc_relative = true,
    },
    [WST_VM_INSTR_FORMAT_U] = {
        .rd_shift = 7,
        .imm_shift = 12, .imm_bits = 20,
    },
    [WST_VM_INSTR_FORMAT_J] = {
        .rd_shift = 7,
        .imm_shift = 10, .imm_bits = 22, .imm_signed = true,
        .pc_relative = true,
    },
    [WST_VM_INSTR_FORMAT_R4] = {
        .rd_shift = 7, .rs1_shift = 10, .rs2_shift = 13, .rs3_shift = 16,
    },
};

const WstVmInstrDesc wst_vm_op_table[WST_VM_NUM_OPCODES] = {
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

    [WST_VM_LWU]      = { "lwu",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_lwu, 4 },

    [WST_VM_SLT]      = { "slt",      WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_slt, 1 },

    [WST_VM_SLTU]     = { "sltu",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_sltu, 1 },

    [WST_VM_BLTU]     = { "bltu",     WST_VM_INSTR_FORMAT_B,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_bltu, 1 },

    [WST_VM_BGEU]     = { "bgeu",     WST_VM_INSTR_FORMAT_B,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_bgeu, 1 },

    [WST_VM_ADDW]     = { "addw",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_addw, 1 },

    [WST_VM_SUBW]     = { "subw",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_subw, 1 },

    [WST_VM_MULW]     = { "mulw",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_mulw, 1 },

    [WST_VM_DIVW]     = { "divw",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_divw, 1 },

    [WST_VM_DIVUW]    = { "divuw",    WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_divuw, 1 },

    [WST_VM_REMW]     = { "remw",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_remw, 1 },

    [WST_VM_REMUW]    = { "remuw",    WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_remuw, 1 },

    [WST_VM_SLLW]     = { "sllw",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_sllw, 1 },

    [WST_VM_SRLW]     = { "srlw",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_srlw, 1 },

    [WST_VM_SRAW]     = { "sraw",     WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_NONE, vm_exec_sraw, 1 },

    [WST_VM_ADDIW]    = { "addiw",    WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_addiw, 1 },

    [WST_VM_SLLIW]    = { "slliw",    WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_slliw, 1 },

    [WST_VM_SRLIW]    = { "srliw",    WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_srliw, 1 },

    [WST_VM_SRAIW]    = { "sraiw",    WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_NONE, vm_exec_sraiw, 1 },

    [WST_VM_FLW]      = { "flw",      WST_VM_INSTR_FORMAT_I,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_IMM,
                          WST_VM_OPND_RD, vm_exec_flw, 4 },

    [WST_VM_FSW]      = { "fsw",      WST_VM_INSTR_FORMAT_S,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_IMM,
                          WST_VM_OPND_RS2, vm_exec_fsw, 4 },

    [WST_VM_FCVT_S_W] = { "fcvt.s.w", WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1,
                          WST_VM_OPND_RD, vm_exec_fcvt_s_w, 1 },

    [WST_VM_FCVT_W_S] = { "fcvt.w.s", WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1,
                          WST_VM_OPND_RS1, vm_exec_fcvt_w_s, 1 },

    [WST_VM_FADD_S]   = { "fadd.s",   WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fadd_s, 1 },

    [WST_VM_FSUB_S]   = { "fsub.s",   WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fsub_s, 1 },

    [WST_VM_FMUL_S]   = { "fmul.s",   WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fmul_s, 1 },

    [WST_VM_FDIV_S]   = { "fdiv.s",   WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fdiv_s, 1 },

    [WST_VM_FSQRT_S]  = { "fsqrt.s",  WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1, vm_exec_fsqrt_s, 1 },

    [WST_VM_FEQ_S]    = { "feq.s",    WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_feq_s, 1 },

    [WST_VM_FLT_S]    = { "flt.s",    WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_flt_s, 1 },

    [WST_VM_FLE_S]    = { "fle.s",    WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fle_s, 1 },

    [WST_VM_FABS_S]   = { "fabs.s",   WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1, vm_exec_fabs_s, 1 },

    [WST_VM_FNEG_S]   = { "fneg.s",   WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1, vm_exec_fneg_s, 1 },

    [WST_VM_FMAX_S]   = { "fmax.s",   WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fmax_s, 1 },

    [WST_VM_FMIN_S]   = { "fmin.s",   WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2, vm_exec_fmin_s, 1 },

    [WST_VM_FMV_S]    = { "fmv.s",    WST_VM_INSTR_FORMAT_R,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1, vm_exec_fmv_s, 1 },

    [WST_VM_FMADD]    = { "fmadd",    WST_VM_INSTR_FORMAT_R4,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3, vm_exec_fmadd, 1 },

    [WST_VM_FMSUB]    = { "fmsub",    WST_VM_INSTR_FORMAT_R4,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3, vm_exec_fmsub, 1 },

    [WST_VM_FNMADD]   = { "fnmadd",   WST_VM_INSTR_FORMAT_R4,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3, vm_exec_fnmadd, 1 },

    [WST_VM_FNMSUB]   = { "fnmsub",   WST_VM_INSTR_FORMAT_R4,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3, vm_exec_fnmsub, 1 },

    [WST_VM_FMADD_S]  = { "fmadd.s",  WST_VM_INSTR_FORMAT_R4,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3, vm_exec_fmadd_s, 1 },

    [WST_VM_FMSUB_S]  = { "fmsub.s",  WST_VM_INSTR_FORMAT_R4,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3, vm_exec_fmsub_s, 1 },

    [WST_VM_FNMADD_S] = { "fnmadd.s", WST_VM_INSTR_FORMAT_R4,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3, vm_exec_fnmadd_s, 1 },

    [WST_VM_FNMSUB_S] = { "fnmsub.s", WST_VM_INSTR_FORMAT_R4,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3,
                          WST_VM_OPND_RD  | WST_VM_OPND_RS1 | WST_VM_OPND_RS2 | WST_VM_OPND_RS3, vm_exec_fnmsub_s, 1 },

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
