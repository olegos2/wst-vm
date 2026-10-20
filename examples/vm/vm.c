#include "toys/argparse.h"
#include "toys/debug.h"
#include "toys/vm.h"

#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define ARR_LEN(arr) (sizeof(arr) / sizeof(*arr))

/* Library defaults in MiB and KiB. */
#define VM_DEFAULT_MEM_MB ((int)(WST_VM_MEM_DEFAULT_SIZE / (1024 * 1024)))
#define VM_DEFAULT_STACK_KB ((int)(WST_VM_STACK_DEFAULT_SIZE / 1024))

/* Default framebuffer size in pixels. */
#define VM_DEFAULT_FB_WIDTH 320
#define VM_DEFAULT_FB_HEIGHT 240

/* Default and largest integer window scale. */
#define VM_DEFAULT_FB_SCALE 1
#define VM_FB_MAX_SCALE 8

/* Fallback frame rate when the monitor reports none. */
#define VM_FB_DEFAULT_FPS 60

static WstVm vm;
static WstVmContext ctx;

static void vm_cleanup(void)
{
    wst_vm_close(&vm);
    wst_vm_context_free(&ctx);
}

/* Run guest instructions until it asks for the next frame or stops. */
static WstVmRet vm_run_until_flush(WstVm *machine)
{
    for (;;) {
        WstVmInstr instr = { 0 };
        if (!wst_vm_decode(machine, &instr))
            return WST_VM_RUN_ERROR;

        WstVmRet ret = wst_vm_execute(machine, &instr);
        if (ret != WST_VM_RUN_CONTINUE)
            return ret;
    }
}

/* Show the guest framebuffer in a raylib window until it closes. */
static int run_fb(WstVm *machine, int scale)
{
    WstVmContext *context = machine->ctx;
    int width = (int)context->config.fb_width;
    int height = (int)context->config.fb_height;
    int window_width = width * scale;
    int window_height = height * scale;
    uint8_t *pixels = context->mem + (size_t)context->config.fb_addr;

    InitWindow(window_width, window_height, "wst-vm");
    if (!IsWindowReady()) {
        LOG_E("cannot open fb window");
        return EXIT_FAILURE;
    }

    Image image = {
        .data = pixels,
        .width = width,
        .height = height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    Texture2D texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);

    int refresh = GetMonitorRefreshRate(GetCurrentMonitor());
    SetTargetFPS(refresh > 0 ? refresh : VM_FB_DEFAULT_FPS);

    bool ok = true;
    int guest_code = EXIT_SUCCESS;
    while (!WindowShouldClose()) {
        WstVmRet ret = vm_run_until_flush(machine);
        if (ret == WST_VM_RUN_ERROR) {
            ok = false;
            break;
        }
        if (ret == WST_VM_RUN_EXIT) {
            guest_code = (int)(machine->regs[2] & 0xFFu);
            break;
        }

        UpdateTexture(texture, pixels);
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTextureEx(texture, (Vector2){ 0 }, 0.0f, (float)scale, WHITE);
        EndDrawing();
    }

    UnloadTexture(texture);
    CloseWindow();
    return ok ? guest_code : EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    bool help = false;
    bool verbose = false;
    bool fb = false;
    int mem_mb = 0;
    int stack_kb = 0;
    int fb_width = 0;
    int fb_height = 0;
    int fb_scale = 0;
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
            .description = "guest RAM in MiB (default 256)",
        },
        {
            .type = ARG_INT,
            .dest = &stack_kb,
            .long_name = "--stack-size",
            .description = "stack size in KiB (default 64)",
        },
        {
            .type = ARG_SWITCH,
            .dest = &fb,
            .long_name = "--fb",
            .description = "enable framebuffer window",
        },
        {
            .type = ARG_INT,
            .dest = &fb_width,
            .long_name = "--fb-width",
            .description = "framebuffer width (default 320, max 640)",
        },
        {
            .type = ARG_INT,
            .dest = &fb_height,
            .long_name = "--fb-height",
            .description = "framebuffer height (default 240, max 480)",
        },
        {
            .type = ARG_INT,
            .dest = &fb_scale,
            .long_name = "--fb-scale",
            .description = "integer window scale (default 1, max 8)",
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
    if (fb_width == 0)
        fb_width = VM_DEFAULT_FB_WIDTH;
    if (fb_height == 0)
        fb_height = VM_DEFAULT_FB_HEIGHT;
    if (fb_scale == 0)
        fb_scale = VM_DEFAULT_FB_SCALE;

    if (fb) {
        if (fb_width < 1 || fb_height < 1 ||
            fb_width > WST_VM_FB_MAX_WIDTH ||
            fb_height > WST_VM_FB_MAX_HEIGHT) {
            fprintf(stderr, "fb size must be 1..%dx%d\n",
                    WST_VM_FB_MAX_WIDTH, WST_VM_FB_MAX_HEIGHT);
            return EXIT_FAILURE;
        }
        if (fb_scale < 1 || fb_scale > VM_FB_MAX_SCALE) {
            fprintf(stderr, "fb scale must be 1..%d\n", VM_FB_MAX_SCALE);
            return EXIT_FAILURE;
        }
        ctx.config.fb_width = (uint32_t)fb_width;
        ctx.config.fb_height = (uint32_t)fb_height;
        ctx.config.fb_format = WST_VM_FB_FORMAT_RGBA8888;
    }

    ctx.mem_size = (size_t)mem_mb * 1024 * 1024;
    ctx.stack.size = (size_t)stack_kb * 1024;

    if (!wst_vm_context_load(&ctx, prog_path))
        return EXIT_FAILURE;
    wst_vm_init(&vm, &ctx);
    atexit(vm_cleanup);

    if (ctx.config.fb_addr != 0)
        return run_fb(&vm, fb_scale);

    WstVmInstr instr = { 0 };
    while (1) {
        if (!wst_vm_decode(&vm, &instr))
            return EXIT_FAILURE;
        WstVmRet ret = wst_vm_execute(&vm, &instr);
        if (ret == WST_VM_RUN_EXIT)
            return (int)(vm.regs[2] & 0xFFu);
    }
}
