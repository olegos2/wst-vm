#include "toys/argparse.h"
#include "toys/debug.h"
#include "toys/vm.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define ARR_LEN(arr) (sizeof(arr) / sizeof(*arr))

/* Defaults matching WST_VM_MEM_DEFAULT_MAX / WST_VM_STACK_DEFAULT_SIZE. */
#define VM_DEFAULT_MEM_MB 1024
#define VM_DEFAULT_STACK_KB 64

static WstVm vm;
static WstVmContext ctx;

static void vm_cleanup(void)
{
    wst_vm_close(&vm);
    wst_vm_context_free(&ctx);
}

int main(int argc, char **argv)
{
    bool help = false;
    bool verbose = false;
    int mem_mb = 0;
    int stack_kb = 0;
    const char *prog_path = NULL;

    ArgOption opts[] = {
        {
            .type = ARG_SWITCH,
            .dest = &help,
            .short_name = "-h",
            .long_name = "--help",
            .description = "print this help and exit",
        },
#ifdef WST_DEBUG
        {
            .type = ARG_SWITCH,
            .dest = &verbose,
            .short_name = "-v",
            .long_name = "--verbose",
            .description = "enable verbose logging messages",
        },
#endif
        {
            .type = ARG_INT,
            .dest = &mem_mb,
            .long_name = "--mem-max",
            .description = "address space cap in MiB (default 1024)",
        },
        {
            .type = ARG_INT,
            .dest = &stack_kb,
            .long_name = "--stack-size",
            .description = "stack size in KiB (default 64)",
        },
        {
            .type = ARG_POSITIONAL,
            .dest = &prog_path,
            .long_name = "prog_filename",
            .description = "program file path to run",
            .required = true,
        },
    };

    ArgParser parser = {
        .prog = argv[0],
        .opts = opts,
        .nopts = ARR_LEN(opts),
    };

    if (!argparse_parse(&parser, argc, argv)) {
        if (help)
            argparse_print_help(&parser);
        else
            fprintf(stderr, "%s, run '%s --help' for usage\n", parser.error, argv[0]);
        return EXIT_FAILURE;
    }

    if (help) {
        argparse_print_help(&parser);
        return EXIT_SUCCESS;
    }

    if (verbose)
        wst_log_set_max_prio(WST_LOG_VERBOSE);

    if (mem_mb < 0 || stack_kb < 0) {
        fprintf(stderr, "mem-max and stack-size must be positive\n");
        return EXIT_FAILURE;
    }
    if (mem_mb == 0)
        mem_mb = VM_DEFAULT_MEM_MB;
    if (stack_kb == 0)
        stack_kb = VM_DEFAULT_STACK_KB;

    ctx.mem_max = (size_t)mem_mb * 1024 * 1024;
    ctx.stack_size = (size_t)stack_kb * 1024;

    if (!wst_vm_init(&vm))
        return EXIT_FAILURE;
    atexit(vm_cleanup);

    if (!wst_vm_context_load(&ctx, prog_path))
        return EXIT_FAILURE;
    vm.pc = ctx.entry;
    vm.regs[WST_VM_REG_SP] = (uint64_t)ctx.stack_top;

    WstVmInstr instr = { 0 };
    while (1) {
        if (!wst_vm_decode(&vm, &ctx, &instr))
            return EXIT_FAILURE;
        wst_vm_execute(&vm, &ctx, &instr);
    }
}
