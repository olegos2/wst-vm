#ifndef TOYS_PROG_H
#define TOYS_PROG_H

#include "toys/vec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Program file signature in the beginning.
 */
#define WST_PROG_MAGIC "\x7fWST"

/**
 * Loader-supported format version.
 */
#define WST_PROG_VERSION 1u

/**
 * Executable header similar to ELF ehdr.
 */
typedef struct {
    /** File signature. */
    char magic[4];
    /** Format version. */
    uint32_t version;
    /** Program entry address. */
    uint64_t entry;
    /** Offset of program headers array. */
    uint64_t phoff;
    /** Count of program headers. */
    uint64_t phnum;
} WstProgHeader;

/**
 * Program header, one loadable segment, similar to ELF phdr.
 */
typedef struct {
    uint64_t offset;
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
} WstProgSegment;

/**
 * Load program file segments into memory, zero-filling the rest.
 * Validates the whole file before touching memory.
 * Entry point goes to `entry_out`. `mem_max` caps `vaddr + memsz`,
 * zero means no cap (caller usually passes its resolved limit).
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_prog_load(WstVecU8 *mem, size_t *entry_out, const char *path,
                   size_t mem_max);

#endif /* TOYS_PROG_H */
