#include "toys/vm.h"
#include "vm_priv.h"
#include "toys/debug.h"
#include "toys/prog.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int64_t vm_sign_ext(uint32_t field, unsigned width)
{
    assert(width > 0 && width <= 32);

    uint64_t clean = field & (((uint64_t)1 << width) - 1);
    uint64_t sign = (uint64_t)1 << (width - 1);

    return (int64_t)(clean ^ sign) - (int64_t)sign;
}

/* Zero means `WST_VM_STACK_DEFAULT_SIZE`. */
static size_t vm_stack_size(const WstVmContext *ctx)
{
    assert(ctx != NULL);

    return ctx->stack.size != 0 ? ctx->stack.size : WST_VM_STACK_DEFAULT_SIZE;
}

/* Round `addr` down to a multiple of `align`, a power of two. */
static size_t vm_align_down(size_t addr, size_t align)
{
    assert(align != 0 && (align & (align - 1)) == 0);

    return addr & ~(align - 1);
}

/* Validate the framebuffer request and place it below `stack_base`. */
static bool vm_fb_layout(WstVmConfig *config, size_t stack_base)
{
    assert(config != NULL);

    if (config->fb_width == 0 && config->fb_height == 0) {
        config->fb_addr = 0;
        config->fb_stride = 0;
        config->fb_format = 0;
        return true;
    }

    if (config->fb_width == 0 || config->fb_height == 0) {
        LOG_E("fb needs both width and height");
        return false;
    }
    if (config->fb_width > WST_VM_FB_MAX_WIDTH ||
        config->fb_height > WST_VM_FB_MAX_HEIGHT) {
        LOG_E("fb %" PRIu32 "x%" PRIu32 " past %dx%d",
              config->fb_width, config->fb_height,
              WST_VM_FB_MAX_WIDTH, WST_VM_FB_MAX_HEIGHT);
        return false;
    }
    if (config->fb_format != (uint32_t)WST_VM_FB_FORMAT_RGBA8888) {
        LOG_E("unsupported fb format %" PRIu32, config->fb_format);
        return false;
    }

    size_t stride = (size_t)config->fb_width * 4;
    size_t size = stride * config->fb_height;
    if (size > stack_base || sizeof(*config) > stack_base - size) {
        LOG_E("fb of 0x%08zx bytes past stack", size);
        return false;
    }

    config->fb_stride = (uint32_t)stride;
    config->fb_addr = (uint64_t)vm_align_down(stack_base - size, 8);
    return true;
}

static void vm_mem_ensure(WstVmContext *ctx, uint64_t addr, uint64_t size)
{
    if (size > ctx->mem_size || addr > (uint64_t)ctx->mem_size - size) {
        LOG_E("address 0x%08" PRIx64 " past mem end 0x%08zx",
              addr, ctx->mem_size);
        exit(EXIT_FAILURE);
    }
}

/* Size-specific accessors, misaligned access is fatal. */
#define VM_DEF_MEM_RW(bits) \
    U(bits) vm_mem_read##bits(WstVm *vm, uint64_t addr) \
    { \
        WstVmContext *ctx = vm->ctx; \
        vm_mem_ensure(ctx, addr, (bits) / 8); \
        if (addr % ((bits) / 8) != 0) { \
            LOG_E("misaligned " #bits "-bit access at 0x%08" PRIx64, addr); \
            exit(EXIT_FAILURE); \
        } \
        return *(U(bits) *)&ctx->mem[(size_t)addr]; \
    } \
    \
    void vm_mem_write##bits(WstVm *vm, uint64_t addr, U(bits) value) \
    { \
        WstVmContext *ctx = vm->ctx; \
        vm_mem_ensure(ctx, addr, (bits) / 8); \
        if (addr % ((bits) / 8) != 0) { \
            LOG_E("misaligned " #bits "-bit access at 0x%08" PRIx64, addr); \
            exit(EXIT_FAILURE); \
        } \
        *(U(bits) *)&ctx->mem[(size_t)addr] = value; \
    }

VM_DEF_MEM_RW(64)
VM_DEF_MEM_RW(32)

void vm_stack_push64(WstVm *vm, size_t pc, uint64_t value)
{
    WstVmContext *ctx = vm->ctx;
    uint64_t sp = R_SP;

    if (sp % 8 != 0) {
        LOG_E("misaligned sp 0x%08" PRIx64 " at pc 0x%08zx", sp, pc);
        exit(EXIT_FAILURE);
    }
    if (sp < (uint64_t)ctx->stack.base + 8 || sp > (uint64_t)ctx->stack.top) {
        LOG_E("stack overflow sp 0x%08" PRIx64 " at pc 0x%08zx", sp, pc);
        exit(EXIT_FAILURE);
    }

    R_SP = sp - 8;
    vm_mem_write64(vm, sp - 8, value);
}

uint64_t vm_stack_pop64(WstVm *vm, size_t pc)
{
    WstVmContext *ctx = vm->ctx;
    uint64_t sp = R_SP;

    if (sp % 8 != 0) {
        LOG_E("misaligned sp 0x%08" PRIx64 " at pc 0x%08zx", sp, pc);
        exit(EXIT_FAILURE);
    }
    if (sp >= (uint64_t)ctx->stack.top || sp < (uint64_t)ctx->stack.base) {
        LOG_E("stack underflow sp 0x%08" PRIx64 " at pc 0x%08zx", sp, pc);
        exit(EXIT_FAILURE);
    }

    R_SP = sp + 8;
    return vm_mem_read64(vm, sp);
}

void wst_vm_init(WstVm *vm, WstVmContext *ctx)
{
    assert(vm != NULL);
    assert(ctx != NULL);

    for (size_t i = 0; i < WST_VM_NUM_GPR; i++)
        vm->regs[i] = 0;
    for (size_t i = 0; i < WST_VM_NUM_FP; i++)
        vm->fp[i] = 0.0;
    vm->pc = ctx->entry;
    vm->ctx = ctx;
    vm->regs[WST_VM_REG_SP] = (uint64_t)ctx->stack.top;
    vm->regs[1] = (uint64_t)ctx->config_addr;
}

bool wst_vm_context_load(WstVmContext *ctx, const char *path)
{
    assert(ctx != NULL);
    assert(path != NULL);

    free(ctx->mem);
    ctx->mem = NULL;
    ctx->entry = 0;
    ctx->config_addr = 0;
    ctx->stack.base = 0;
    ctx->stack.top = 0;

    size_t mem_size = ctx->mem_size != 0 ? ctx->mem_size : WST_VM_MEM_DEFAULT_SIZE;
    size_t top = mem_size & ~(size_t)7;
    size_t stack_size = vm_stack_size(ctx);
    if (stack_size > top) {
        LOG_E("stack of 0x%08zx bytes past mem end 0x%08zx", stack_size, top);
        return false;
    }
    stack_size = (stack_size + 7) & ~(size_t)7;

    size_t base = top - stack_size;
    if (!vm_fb_layout(&ctx->config, base))
        return false;

    size_t config_addr = base;
    if (ctx->config.fb_addr != 0)
        config_addr = (size_t)ctx->config.fb_addr;
    if (config_addr < sizeof(ctx->config)) {
        LOG_E("no room for firmware config below stack");
        return false;
    }
    config_addr = vm_align_down(config_addr - sizeof(ctx->config), 8);

    uint8_t *mem = calloc(mem_size, sizeof(uint8_t));
    if (mem == NULL) {
        LOG_E("cannot reserve 0x%08zx bytes", mem_size);
        return false;
    }

    /* Direct dereferences need an 8-aligned base. */
    assert(((uintptr_t)mem & 7u) == 0);

    size_t entry = 0;
    if (!wst_prog_load(mem, config_addr, &entry, path)) {
        free(mem);
        return false;
    }

    ctx->config.magic = WST_VM_CONFIG_MAGIC;
    ctx->config.version = WST_VM_CONFIG_VERSION;
    ctx->config.mem_size = (uint64_t)mem_size;
    ctx->config.stack_top = (uint64_t)top;
    memcpy(&mem[config_addr], &ctx->config, sizeof(ctx->config));

    ctx->mem = mem;
    ctx->mem_size = mem_size;
    ctx->entry = entry;
    ctx->config_addr = config_addr;
    ctx->stack.base = base;
    ctx->stack.top = top;

    if (ctx->config.fb_addr != 0) {
        LOG_I("fb %" PRIu32 "x%" PRIu32 " at 0x%08" PRIx64,
              ctx->config.fb_width, ctx->config.fb_height,
              ctx->config.fb_addr);
    }
    return true;
}

void wst_vm_context_free(WstVmContext *ctx)
{
    if (ctx == NULL)
        return;

    free(ctx->mem);
    ctx->mem = NULL;
    ctx->mem_size = 0;
    ctx->entry = 0;
    ctx->config_addr = 0;
    ctx->stack.base = 0;
    ctx->stack.top = 0;
}

bool wst_vm_decode_word(uint32_t word, size_t addr, WstVmInstr *ins)
{
    assert(ins != NULL);

    WstVmOp op = (WstVmOp)(word & 0x7Fu);
    const WstVmInstrDesc *desc = &wst_vm_op_table[(unsigned)op];
    if (desc->format == WST_VM_INSTR_FORMAT_INVALID) {
        LOG_E("unknown opcode 0x%02x at 0x%08zx", (unsigned)op, addr);
        return false;
    }

    ins->op = op;
    ins->rd = 0;
    ins->rs1 = 0;
    ins->rs2 = 0;
    ins->rs3 = 0;
    ins->imm = 0;
    ins->addr = addr;

    const WstVmInstrFormatDesc *fmt = &wst_vm_format_table[desc->format];

    if (fmt->rd_shift != 0)
        ins->rd = (uint8_t)((word >> fmt->rd_shift) & 0x7u);
    if (fmt->rs1_shift != 0)
        ins->rs1 = (uint8_t)((word >> fmt->rs1_shift) & 0x7u);
    if (fmt->rs2_shift != 0)
        ins->rs2 = (uint8_t)((word >> fmt->rs2_shift) & 0x7u);
    if (fmt->rs3_shift != 0)
        ins->rs3 = (uint8_t)((word >> fmt->rs3_shift) & 0x7u);

    if (fmt->imm_shift != 0) {
        uint32_t field = word >> fmt->imm_shift;

        if (fmt->imm_signed) {
            ins->imm = vm_sign_ext(field, fmt->imm_bits);
        } else {
            uint32_t mask = (1u << fmt->imm_bits) - 1u;
            ins->imm = (int64_t)(field & mask) << fmt->imm_shift;
        }
    }

    return true;
}

/* Append text to the output buffer. */
static bool vm_text_put(const char *text, char *buf, size_t cap,
                        size_t *len_inout)
{
    int n = snprintf(buf + *len_inout, cap - *len_inout, "%s", text);
    if (n < 0 || (size_t)n >= cap - *len_inout)
        return false;

    *len_inout += (size_t)n;
    return true;
}

bool wst_vm_instr_text(const WstVmInstr *ins, const char *imm_text,
                       char *buf, size_t cap)
{
    assert(ins != NULL);
    assert(buf != NULL);

    if (cap == 0)
        return false;
    buf[0] = '\0';

    const WstVmInstrDesc *desc = &wst_vm_op_table[(unsigned)ins->op];
    if (desc->mnemonic == NULL)
        return false;

    size_t len = 0;
    if (!vm_text_put(desc->mnemonic, buf, cap, &len))
        return false;

    bool first = true;

    for (unsigned slot = WST_VM_OPND_RD; slot <= WST_VM_OPND_IMM; slot <<= 1) {
        if ((desc->operands & slot) == 0)
            continue;

        if (!vm_text_put(first ? " " : ", ", buf, cap, &len))
            return false;
        first = false;

        if (slot == WST_VM_OPND_IMM) {
            if (imm_text != NULL) {
                if (!vm_text_put(imm_text, buf, cap, &len))
                    return false;
                continue;
            }

            int n = snprintf(buf + len, cap - len, "%" PRId64, ins->imm);
            if (n < 0 || (size_t)n >= cap - len)
                return false;
            len += (size_t)n;
            continue;
        }

        unsigned reg = slot == WST_VM_OPND_RD ? ins->rd :
                       slot == WST_VM_OPND_RS1 ? ins->rs1 :
                       slot == WST_VM_OPND_RS2 ? ins->rs2 : ins->rs3;
        char rname[4] = { (char)(((desc->fregs & slot) != 0) ? 'f' : 'x'),
                          (char)('0' + reg), '\0' };
        if (!vm_text_put(rname, buf, cap, &len))
            return false;
    }

    return true;
}

bool wst_vm_decode(const WstVm *vm, WstVmInstr *ins)
{
    assert(vm != NULL);
    assert(vm->ctx != NULL);
    assert(ins != NULL);

    const WstVmContext *ctx = vm->ctx;

    if (vm->pc % WST_VM_INSTR_SIZE != 0) {
        LOG_E("misaligned pc 0x%08zx", vm->pc);
        return false;
    }

    if (vm->pc >= ctx->mem_size ||
        ctx->mem_size - vm->pc < WST_VM_INSTR_SIZE) {
        LOG_E("pc 0x%08zx past mem end 0x%08zx", vm->pc, ctx->mem_size);
        return false;
    }

    /* Fetch never grows memory, past-end is a decode error (checked above).
     * `pc` is 4-aligned here, so this is an aligned dereference. */
    uint32_t word = *(const uint32_t *)&ctx->mem[vm->pc];

    if (!wst_vm_decode_word(word, vm->pc, ins))
        return false;

#ifdef WST_DEBUG
    char text[WST_VM_INSTR_TEXT_CAP] = { 0 };
    if (wst_vm_instr_text(ins, NULL, text, sizeof(text)))
        LOG_V("0x%08zx  %s", vm->pc, text);
#endif

    return true;
}

WstVmRet wst_vm_execute(WstVm *vm, const WstVmInstr *ins)
{
    assert(vm != NULL);
    assert(vm->ctx != NULL);
    assert(ins != NULL);

    /* Decode only fills valid ops, so handler always exists. */
    WstVmInstrHandler handler = wst_vm_op_table[(unsigned)ins->op].handler;
    assert(handler != NULL);

    WstVmRet ret = WST_VM_RUN_CONTINUE;
    if (ins->op == WST_VM_ECALL) {
        if (ECALL_NUM == WST_VM_SYS_FLUSH)
            ret = WST_VM_RUN_FLUSH;
        else if (ECALL_NUM == WST_VM_SYS_EXIT)
            ret = WST_VM_RUN_EXIT;
    }

    vm->pc = ins->addr + WST_VM_INSTR_SIZE;
    handler(vm, ins);

    return ret;
}

void wst_vm_close(WstVm *vm)
{
    if (vm == NULL)
        return;

    vm->pc = 0;
    vm->ctx = NULL;
}
