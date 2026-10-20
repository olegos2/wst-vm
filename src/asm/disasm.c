#include "toys/asm.h"
#include "toys/debug.h"
#include "toys/file.h"
#include "toys/prog.h"
#include "toys/vm.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Data bytes per disassembly line. */
#define DISASM_DATA_LINE 16

/* Code and data slices of a prog file. */
typedef struct {
    WstProgHeader hdr;
    WstProgSegment code;
    WstProgSegment data;
    bool has_data;
} DisasmLayout;

/* Read the header and segments, check the text plus data layout. */
static bool disasm_layout(const char *path, const WstFile *file,
                          DisasmLayout *out)
{
    assert(path != NULL);
    assert(file != NULL);
    assert(out != NULL);

    if (file->size < sizeof(out->hdr)) {
        LOG_E("'%s' too small for prog header", path);
        return false;
    }
    memcpy(&out->hdr, file->data, sizeof(out->hdr));

    if (memcmp(out->hdr.magic, WST_PROG_MAGIC, sizeof(out->hdr.magic)) != 0 ||
        out->hdr.version != WST_PROG_VERSION) {
        LOG_E("'%s' is not a prog file", path);
        return false;
    }

    if (out->hdr.phnum < 1 || out->hdr.phnum > 2 ||
        out->hdr.phoff > file->size ||
        out->hdr.phnum >
            (file->size - (size_t)out->hdr.phoff) / sizeof(WstProgSegment)) {
        LOG_E("'%s' needs one or two program headers", path);
        return false;
    }

    memcpy(&out->code, file->data + (size_t)out->hdr.phoff,
           sizeof(out->code));

    out->has_data = out->hdr.phnum == 2;
    if (out->has_data) {
        memcpy(&out->data,
               file->data + (size_t)out->hdr.phoff + sizeof(WstProgSegment),
               sizeof(out->data));
    }

    size_t code_len = (size_t)out->code.filesz;
    size_t code_off = (size_t)out->code.offset;

    if (out->code.vaddr != 0 || out->code.memsz != out->code.filesz ||
        code_len % WST_VM_INSTR_SIZE != 0) {
        LOG_E("'%s' code is not flat at address zero", path);
        return false;
    }
    if (code_off > file->size || code_len > file->size - code_off) {
        LOG_E("'%s' code past file end", path);
        return false;
    }

    if (out->has_data) {
        size_t data_len = (size_t)out->data.filesz;
        size_t data_off = (size_t)out->data.offset;

        if (out->data.vaddr != (uint64_t)wst_prog_data_addr(code_len) ||
            out->data.memsz != out->data.filesz) {
            LOG_E("'%s' data is not right after code", path);
            return false;
        }
        if (data_off > file->size || data_len > file->size - data_off) {
            LOG_E("'%s' data past file end", path);
            return false;
        }
    }

    if (out->hdr.entry % WST_VM_INSTR_SIZE != 0 ||
        out->hdr.entry >= out->code.filesz) {
        LOG_E("'%s' has bad entry point", path);
        return false;
    }

    return true;
}

/* Branch or jump target, false for other formats and negative targets. */
static bool disasm_target(const WstVmInstr *ins, size_t *target_out)
{
    assert(ins != NULL);
    assert(target_out != NULL);

    const WstVmInstrDesc *desc = &wst_vm_op_table[(unsigned)ins->op];
    if (!wst_vm_format_table[desc->format].pc_relative)
        return false;

    int64_t target = (int64_t)ins->addr + ins->imm;
    if (target < 0)
        return false;

    *target_out = (size_t)target;
    return true;
}

/* Local label of `target`, NULL when it is outside the listing. */
static const char *disasm_local_label(char *buf, size_t cap, size_t target,
                                      size_t count, size_t entry)
{
    assert(buf != NULL);

    if (target % WST_VM_INSTR_SIZE != 0 ||
        target / WST_VM_INSTR_SIZE >= count)
        return NULL;

    if (target == entry)
        return ":_start";

    snprintf(buf, cap, ":label_%08zx", target);
    return buf;
}

/* Mark instruction addresses that branches and jumps land on. */
static void disasm_mark_labels(const WstVmInstr *ins, size_t count,
                               uint8_t *labels)
{
    assert(ins != NULL);
    assert(labels != NULL);

    for (size_t i = 0; i < count; i++) {
        size_t target = 0;
        if (!disasm_target(&ins[i], &target))
            continue;
        if (target % WST_VM_INSTR_SIZE != 0 ||
            target / WST_VM_INSTR_SIZE >= count)
            continue;
        labels[target / WST_VM_INSTR_SIZE] = 1;
    }
}

/* Decode `count` code words into `ins`. */
static bool disasm_decode(const uint8_t *code, size_t count, WstVmInstr *ins)
{
    assert(code != NULL);
    assert(ins != NULL);

    for (size_t i = 0; i < count; i++) {
        uint32_t word = 0;
        memcpy(&word, code + i * WST_VM_INSTR_SIZE, sizeof(word));

        if (!wst_vm_decode_word(word, i * WST_VM_INSTR_SIZE, &ins[i]))
            return false;
    }

    return true;
}

/* Print one instruction with a label when something lands on it. */
static bool disasm_line(FILE *out, const WstVmInstr *ins,
                        const uint8_t *labels, size_t count, size_t entry)
{
    assert(out != NULL);
    assert(ins != NULL);
    assert(labels != NULL);

    size_t addr = ins->addr;
    if (addr == entry)
        fprintf(out, ":_start\n");
    else if (labels[addr / WST_VM_INSTR_SIZE] != 0)
        fprintf(out, ":label_%08zx\n", addr);

    size_t target = 0;
    char label[32] = { 0 };
    const char *imm_text = NULL;
    if (disasm_target(ins, &target))
        imm_text = disasm_local_label(label, sizeof(label), target, count,
                                      entry);

    char text[WST_VM_INSTR_TEXT_CAP] = { 0 };
    if (!wst_vm_instr_text(ins, imm_text, text, sizeof(text)))
        return false;

    fprintf(out, "    %-28s # 0x%08zx\n", text, addr);
    return true;
}

/* Print data as raw bytes, one label per line. */
static void disasm_data(FILE *out, const WstFile *file,
                        const DisasmLayout *layout)
{
    assert(out != NULL);
    assert(file != NULL);
    assert(layout != NULL);

    const uint8_t *data =
        (const uint8_t *)file->data + (size_t)layout->data.offset;
    size_t len = (size_t)layout->data.filesz;

    fprintf(out, "\nsection .data\n");
    for (size_t off = 0; off < len; off += DISASM_DATA_LINE) {
        size_t end = off + DISASM_DATA_LINE;
        if (end > len)
            end = len;

        fprintf(out, "    data_%08zx: bytes",
                (size_t)layout->data.vaddr + off);
        for (size_t i = off; i < end; i++)
            fprintf(out, " %02x", data[i]);
        fprintf(out, "\n");
    }
}

/* Print the program as assembly text. */
static bool disasm_print(FILE *out, const WstFile *file,
                         const DisasmLayout *layout, size_t max_words)
{
    assert(out != NULL);
    assert(file != NULL);
    assert(layout != NULL);

    const uint8_t *code =
        (const uint8_t *)file->data + (size_t)layout->code.offset;
    size_t words = (size_t)layout->code.filesz / WST_VM_INSTR_SIZE;
    size_t limit = words;
    if (max_words > 0 && max_words < limit)
        limit = max_words;

    WstVmInstr *ins = calloc(limit, sizeof(*ins));
    uint8_t *labels = calloc(limit, sizeof(*labels));
    if (ins == NULL || labels == NULL) {
        LOG_E("cannot reserve %zu instructions", limit);
        free(ins);
        free(labels);
        return false;
    }

    bool ok = disasm_decode(code, limit, ins);
    if (ok)
        disasm_mark_labels(ins, limit, labels);

    if (ok)
        fprintf(out, "section .text\n");

    for (size_t i = 0; ok && i < limit; i++)
        ok = disasm_line(out, &ins[i], labels, limit,
                         (size_t)layout->hdr.entry);

    if (ok && max_words == 0 && layout->has_data && layout->data.filesz > 0)
        disasm_data(out, file, layout);

    free(ins);
    free(labels);
    return ok;
}

bool wst_vm_disasm(FILE *out, const char *path, size_t max_words)
{
    assert(out != NULL);
    assert(path != NULL);

    WstFile file = { 0 };
    if (!wst_file_open(path, &file))
        return false;

    DisasmLayout layout = { 0 };
    bool ok = disasm_layout(path, &file, &layout) &&
              disasm_print(out, &file, &layout, max_words) &&
              !ferror(out);

    wst_file_close(&file);
    return ok;
}
