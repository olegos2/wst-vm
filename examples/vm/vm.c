#include "toys/argparse.h"
#include "toys/debug.h"
#include "toys/vm.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define ARR_LEN(arr) (sizeof(arr) / sizeof(*arr))

static WstVm vm;

static void vm_cleanup(void)
{
    wst_vm_close(&vm);
}

int main(int argc, char **argv)
{
    bool help = false;
    const char *prog_path = NULL;

    ArgOption opts[] = {
        {
            .type = ARG_SWITCH,
            .dest = &help,
            .short_name = "-h",
            .long_name = "--help",
            .description = "print this help and exit",
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

    if (!wst_vm_init(&vm))
        return EXIT_FAILURE;
    atexit(vm_cleanup);

    if (!wst_vm_open(&vm, prog_path))
        return EXIT_FAILURE;

    WstVmInstr instr = { 0 };
    while (1) {
        if (!wst_vm_decode(&vm, &instr))
            return EXIT_FAILURE;
        wst_vm_execute(&vm, &instr);
    }
}
