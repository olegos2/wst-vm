#include "toys/vm.h"
#include "toys/debug.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *vm_op_name(WstVmOp op)
{
    switch (op) {
    case WST_VM_HLT:
        return "hlt";
    case WST_VM_PUSHQ:
        return "pushq";
    case WST_VM_POPQ:
        return "popq";
    case WST_VM_ADDSD:
        return "addsd";
    case WST_VM_SUBSD:
        return "subsd";
    case WST_VM_MULSD:
        return "mulsd";
    case WST_VM_DIVSD:
        return "divsd";
    case WST_VM_POWSD:
        return "powsd";
    case WST_VM_OUTD:
        return "outd";
    default:
        return "<?>";
    }
}

bool wst_vm_init(WstVm *vm)
{
    assert(vm != NULL);

    wst_file_close(&vm->prog);
    vm->rip = 0;

    WstStkErr err = wst_stk_double_init(&vm->stack, 0);
    if (err != WST_STK_NO_ERR) {
        LOG_E("cannot init value stack: %s", wst_stk_err_str(err));
        return false;
    }
    return true;
}

bool wst_vm_open(WstVm *vm, const char *filename)
{
    assert(vm != NULL);
    assert(filename != NULL);

    if (!wst_file_open(filename, &vm->prog))
        return false;
    vm->rip = 0;
    LOG_I("loaded '%s': %zu bytes", filename, vm->prog.size);
    return true;
}

void wst_vm_close(WstVm *vm)
{
    if (vm == NULL)
        return;

    WstStkErr err = wst_stk_double_free(&vm->stack);
    if (err != WST_STK_NO_ERR)
        LOG_W("value stack damaged: %s", wst_stk_err_str(err));
    wst_file_close(&vm->prog);
    vm->rip = 0;
}

bool wst_vm_decode(WstVm *vm, WstVmInstr *ins)
{
    assert(vm != NULL);
    assert(ins != NULL);

    if (vm->rip >= vm->prog.size) {
        LOG_E("rip %zu past program end %zu", vm->rip, vm->prog.size);
        return false;
    }

    size_t at = vm->rip;
    WstVmOp op = (WstVmOp)(unsigned char)vm->prog.data[at];

    if (op == WST_VM_PUSHQ) {
        if (vm->prog.size - at < 9) {
            LOG_E("truncated pushq at rip %zu", at);
            return false;
        }
        memcpy(&ins->imm, vm->prog.data + at + 1, sizeof(ins->imm));
        vm->rip = at + 9;
        LOG_V("rip %zu: pushq %g", at, ins->imm);
    } else {
        ins->imm = 0.0;
        vm->rip = at + 1;
        LOG_V("rip %zu: %s", at, vm_op_name(op));
    }
    ins->op = op;

    switch (op) {
    case WST_VM_HLT:
    case WST_VM_PUSHQ:
    case WST_VM_POPQ:
    case WST_VM_ADDSD:
    case WST_VM_SUBSD:
    case WST_VM_MULSD:
    case WST_VM_DIVSD:
    case WST_VM_POWSD:
    case WST_VM_OUTD:
        break;
    default:
        LOG_E("unknown opcode 0x%02x at rip %zu", (unsigned)op, at);
        return false;
    }
    return true;
}

static void vm_pop2(WstVm *vm, double *a, double *b)
{
    if (wst_stk_double_pop(&vm->stack, b) != WST_STK_NO_ERR ||
        wst_stk_double_pop(&vm->stack, a) != WST_STK_NO_ERR) {
        LOG_E("stack underflow at rip %zu", vm->rip);
        exit(EXIT_FAILURE);
    }
}

static void vm_push(WstVm *vm, double value)
{
    WstStkErr err = wst_stk_double_push(&vm->stack, value);
    if (err != WST_STK_NO_ERR) {
        LOG_E("stack push failed at rip %zu: %s", vm->rip, wst_stk_err_str(err));
        exit(EXIT_FAILURE);
    }
}

void wst_vm_execute(WstVm *vm, const WstVmInstr *ins)
{
    assert(vm != NULL);
    assert(ins != NULL);

    double a = 0.0, b = 0.0;

    switch (ins->op) {
    case WST_VM_HLT:
        exit(EXIT_SUCCESS);
        break;
    case WST_VM_PUSHQ:
        vm_push(vm, ins->imm);
        break;
    case WST_VM_POPQ:
        if (wst_stk_double_pop(&vm->stack, NULL) != WST_STK_NO_ERR) {
            LOG_E("stack underflow at rip %zu", vm->rip);
            exit(EXIT_FAILURE);
        }
        break;
    case WST_VM_ADDSD:
        vm_pop2(vm, &a, &b);
        vm_push(vm, a + b);
        break;
    case WST_VM_SUBSD:
        vm_pop2(vm, &a, &b);
        vm_push(vm, a - b);
        break;
    case WST_VM_MULSD:
        vm_pop2(vm, &a, &b);
        vm_push(vm, a * b);
        break;
    case WST_VM_DIVSD:
        vm_pop2(vm, &a, &b);
        vm_push(vm, a / b);
        break;
    case WST_VM_POWSD:
        vm_pop2(vm, &a, &b);
        vm_push(vm, pow(a, b));
        break;
    case WST_VM_OUTD:
        if (wst_stk_double_pop(&vm->stack, &a) != WST_STK_NO_ERR) {
            LOG_E("stack underflow at rip %zu", vm->rip);
            exit(EXIT_FAILURE);
        }
        printf("%g\n", a);
        break;
    }
}
