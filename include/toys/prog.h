#ifndef TOYS_PROG_H
#define TOYS_PROG_H

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
 * Load program file segments into memory.
 * Validates the whole file before touching memory.
 * `mem` must hold `mem_size` zeroed bytes, the image span inside
 * is zeroed again and file parts copied over. Entry point goes
 * to `entry_out`. Segments past `mem_size` are rejected.
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_prog_load(uint8_t *mem, size_t mem_size, size_t *entry_out,
                   const char *path);

/**
 * Data segment address for `code_len` code bytes, first 8-aligned
 * address past the code.
 */
size_t wst_prog_data_addr(size_t code_len);

/**
 * Write code and data buffers as a program file with one text segment
 * at address `0` and one data segment right after the code.
 *
 * @param path filename to write
 * @param entry address of first instruction in code segment to execute
 *
 * @return `true` on success, `false` on failure.
 */
bool wst_prog_save(const char *path, const uint8_t *code, size_t code_len,
                   const uint8_t *data, size_t data_len, size_t entry);

#endif /* TOYS_PROG_H */
