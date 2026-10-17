#include "toys/prog.h"
#include "toys/debug.h"
#include "toys/file.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


/* Read and validate the file header, bounds-checking the phdr array. */
static bool prog_read_header(const char *path, const WstFile *file,
                             WstProgHeader *hdr_out)
{
    assert(path != NULL);
    assert(file != NULL);
    assert(hdr_out != NULL);

    if (file->size < sizeof(WstProgHeader)) {
        LOG_E("'%s' too small for prog header", path);
        return false;
    }

    memcpy(hdr_out, file->data, sizeof(*hdr_out));

    if (memcmp(hdr_out->magic, WST_PROG_MAGIC, sizeof(hdr_out->magic)) != 0) {
        LOG_E("'%s' has bad magic", path);
        return false;
    }

    if (hdr_out->version != WST_PROG_VERSION) {
        LOG_E("'%s' has unsupported version %" PRIu32, path,
              hdr_out->version);
        return false;
    }

    if (hdr_out->phoff > file->size ||
        hdr_out->phnum > (file->size - hdr_out->phoff) / sizeof(WstProgSegment)) {
        LOG_E("'%s' has program headers past file end", path);
        return false;
    }

    return true;
}

/* Fetch one segment by index, header bounds were checked above. */
static void prog_get_seg(const WstFile *file, const WstProgHeader *hdr,
                         uint64_t idx, WstProgSegment *seg_out)
{
    assert(file != NULL);
    assert(hdr != NULL);
    assert(seg_out != NULL);

    memcpy(seg_out, file->data + hdr->phoff + idx * sizeof(*seg_out),
           sizeof(*seg_out));
}

/* Check file bounds, memory size and address cap of a single segment. */
static bool prog_check_seg(const char *path, const WstFile *file,
                           uint64_t idx, const WstProgSegment *seg,
                           size_t mem_max)
{
    assert(path != NULL);
    assert(file != NULL);
    assert(seg != NULL);

    if (seg->filesz > file->size || seg->offset > file->size - seg->filesz) {
        LOG_E("'%s' segment %" PRIu64 " past file end", path,
              idx);
        return false;
    }

    if (seg->memsz < seg->filesz) {
        LOG_E("'%s' segment %" PRIu64 " memory smaller than file part",
              path, idx);
        return false;
    }

    if (mem_max != 0 &&
        (seg->memsz > mem_max || seg->vaddr > mem_max - seg->memsz)) {
        LOG_E("'%s' segment %" PRIu64 " past mem end 0x%08zx",
              path, idx, mem_max);
        return false;
    }

    if (seg->vaddr + seg->memsz < seg->vaddr) {
        LOG_E("'%s' segment %" PRIu64 " address overflows", path,
              idx);
        return false;
    }

    return true;
}

/* Check the new segment against all previous ones for overlap. */
static bool prog_check_overlap(const char *path, const WstFile *file,
                               const WstProgHeader *hdr, uint64_t idx,
                               const WstProgSegment *seg)
{
    assert(path != NULL);
    assert(file != NULL);
    assert(hdr != NULL);
    assert(seg != NULL);

    for (uint64_t j = 0; j < idx; j++) {
        WstProgSegment prev = { 0 };
        prog_get_seg(file, hdr, j, &prev);
        if (seg->vaddr < prev.vaddr + prev.memsz &&
            prev.vaddr < seg->vaddr + seg->memsz) {
            LOG_E("'%s' segments %" PRIu64 " and %" PRIu64 " overlap",
                  path, j, idx);
            return false;
        }
    }

    return true;
}

/* Validate every segment, computing the total span `vaddr + memsz`. */
static bool prog_check_segs(const char *path, const WstFile *file,
                            const WstProgHeader *hdr, size_t mem_max,
                            size_t *total_out)
{
    assert(path != NULL);
    assert(file != NULL);
    assert(hdr != NULL);
    assert(total_out != NULL);

    size_t total = 0;
    for (uint64_t i = 0; i < hdr->phnum; i++) {
        WstProgSegment seg = { 0 };
        prog_get_seg(file, hdr, i, &seg);

        if (!prog_check_seg(path, file, i, &seg, mem_max))
            return false;
        if (!prog_check_overlap(path, file, hdr, i, &seg))
            return false;

        if (seg.vaddr + seg.memsz > total) {
            if (seg.vaddr + seg.memsz > SIZE_MAX) {
                LOG_E("'%s' segment %" PRIu64 " past size_t range",
                      path, i);
                return false;
            }
            total = (size_t)(seg.vaddr + seg.memsz);
        }
    }

    *total_out = total;
    return true;
}

/* Entry must point at a whole 4-byte word inside the span. */
static bool prog_check_entry(const char *path, const WstProgHeader *hdr,
                             size_t total)
{
    assert(path != NULL);
    assert(hdr != NULL);

    if (hdr->entry % 4 != 0 || hdr->entry >= total ||
        total - (size_t)hdr->entry < 4) {
        LOG_E("'%s' has bad entry point", path);
        return false;
    }

    return true;
}

bool wst_prog_load(uint8_t *mem, size_t mem_size, size_t *entry_out,
                   const char *path)
{
    assert(mem != NULL);
    assert(entry_out != NULL);
    assert(path != NULL);

    WstFile file = { 0 };
    if (!wst_file_open(path, &file))
        return false;

    WstProgHeader hdr = { 0 };
    size_t total_segs = 0;

    if (!prog_read_header(path, &file, &hdr) || 
        !prog_check_segs(path, &file, &hdr, mem_size, &total_segs) ||
        !prog_check_entry(path, &hdr, total_segs))
    {
        wst_file_close(&file);
        return false;
    }

    memset(mem, 0, total_segs);
    for (uint64_t i = 0; i < hdr.phnum; i++) {
        WstProgSegment seg = { 0 };
        prog_get_seg(&file, &hdr, i, &seg);

        memcpy(&mem[(size_t)seg.vaddr], file.data + (size_t)seg.offset,
               (size_t)seg.filesz);
    }

    *entry_out = (size_t)hdr.entry;

    LOG_I("loaded '%s': 0x%08zx bytes at entry 0x%08zx",
          path, total_segs, *entry_out);
    wst_file_close(&file);
    return true;
}

#define PROG_DATA_ALIGN (8)

size_t wst_prog_data_addr(size_t code_len)
{
    return (code_len + PROG_DATA_ALIGN - 1) & ~(size_t)(PROG_DATA_ALIGN - 1);
}

bool wst_prog_save(const char *path, const uint8_t *code, size_t code_len,
                   const uint8_t *data, size_t data_len, size_t entry)
{
    assert(path != NULL);
    assert(code_len == 0 || code != NULL);
    assert(data_len == 0 || data != NULL);

    if (code_len % 4 != 0) {
        LOG_E("code of 0x%08zx bytes is not a whole word count", code_len);
        return false;
    }
    if (entry % 4 != 0 || (code_len > 0 && entry >= code_len)) {
        LOG_E("bad entry point 0x%08zx", entry);
        return false;
    }

    size_t data_addr = wst_prog_data_addr(code_len);
    if (data_len > 0 && data_addr > SIZE_MAX - data_len) {
        LOG_E("data segment overflows size_t");
        return false;
    }

    uint64_t phnum = data_len > 0 ? 2 : 1;
    uint64_t phoff = sizeof(WstProgHeader);
    uint64_t code_off = phoff + phnum * sizeof(WstProgSegment);
    uint64_t data_off = code_off + code_len;

    WstProgHeader hdr = { 0 };
    memcpy(hdr.magic, WST_PROG_MAGIC, sizeof(hdr.magic));
    hdr.version = WST_PROG_VERSION;
    hdr.entry = (uint64_t)entry;
    hdr.phoff = phoff;
    hdr.phnum = phnum;

    WstProgSegment text = {
        .offset = code_off,
        .vaddr = 0,
        .filesz = (uint64_t)code_len,
        .memsz = (uint64_t)code_len,
    };
    WstProgSegment blob = {
        .offset = data_off,
        .vaddr = (uint64_t)data_addr,
        .filesz = (uint64_t)data_len,
        .memsz = (uint64_t)data_len,
    };

    FILE *out = fopen(path, "wb");
    if (out == NULL) {
        LOG_E("cannot open '%s' for writing: %s", path, strerror(errno));
        return false;
    }

    bool ok = fwrite(&hdr, sizeof(hdr), 1, out) == 1 &&
              fwrite(&text, sizeof(text), 1, out) == 1 &&
              (data_len == 0 || fwrite(&blob, sizeof(blob), 1, out) == 1) &&
              (code_len == 0 || fwrite(code, 1, code_len, out) == code_len) &&
              (data_len == 0 || fwrite(data, 1, data_len, out) == data_len);

    if (fclose(out) != 0)
        ok = false;

    if (!ok) {
        LOG_E("failed writing '%s': %s", path, strerror(errno));
        return false;
    }

    LOG_I("wrote '%s': 0x%08zx code and 0x%08zx data bytes",
          path, code_len, data_len);
    return true;
}
