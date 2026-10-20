#include "toys/argparse.h"
#include "toys/asm.h"
#include "toys/debug.h"
#include "toys/file.h"
#include "toys/prog.h"
#include "toys/vm.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define ARR_LEN(arr) (sizeof(arr) / sizeof(*arr))


static int run_asm(const char *in_path, const char *out_path)
{
    assert(in_path != NULL);
    assert(out_path != NULL);

    WstFile src = { 0 };
    if (!wst_file_open(in_path, &src))
        return EXIT_FAILURE;

    WstAsmObject obj = { 0 };
    if (!wst_vm_asm_source(src.data, src.size, &obj)) {
        wst_file_close(&src);
        wst_vm_asm_object_free(&obj);
        return EXIT_FAILURE;
    }
    wst_file_close(&src);

    bool ok = wst_prog_save(out_path, (const uint8_t *)obj.code.data,
                            obj.code.length * WST_VM_INSTR_SIZE,
                            obj.data.data, obj.data.length, obj.entry);
    wst_vm_asm_object_free(&obj);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int run_disasm(const char *path, int count)
{
    assert(path != NULL);

    size_t max_words = count > 0 ? (size_t)count : 0;
    return wst_vm_disasm(stdout, path, max_words) ?
           EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    bool help = false;
    bool verbose = false;
    bool disasm = false;
    int count = 0;
    const char *in_path = NULL;
    const char *out_path = NULL;

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
            .type = ARG_SWITCH,
            .dest = &disasm,
            .short_name = "-d",
            .long_name = "--disasm",
            .description = "print prog file as assembly text",
        },
        {
            .type = ARG_INT,
            .dest = &count,
            .long_name = "--count",
            .description = "disassemble at most this many code words",
        },
        {
            .type = ARG_POSITIONAL,
            .dest = &in_path,
            .long_name = "in_filename",
            .description = "input asm text or prog file path",
            .required = true,
        },
        {
            .type = ARG_POSITIONAL,
            .dest = &out_path,
            .long_name = "out_filename",
            .description = "output prog file path (assemble mode)",
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

    if (disasm)
        return run_disasm(in_path, count);

    if (out_path == NULL) {
        fprintf(stderr, "out_filename is required to assemble\n");
        return EXIT_FAILURE;
    }

    return run_asm(in_path, out_path);
}
