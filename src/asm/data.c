#include "asm_priv.h"
#include "toys/debug.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARR_LEN(arr) (sizeof(arr) / sizeof(*arr))

/* Data section entries. */

/* Append one byte to the data buffer. */
static bool asm_data_push(AsmCtx *ctx, uint8_t byte, size_t lineno)
{
    assert(ctx != NULL);

    if (wst_vec_u8_push(&ctx->obj->data, byte) != WST_VEC_NO_ERR) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }
    return true;
}

/* Pad the data buffer with zeros to alignment. */
static bool asm_data_align(AsmCtx *ctx, size_t align, size_t lineno)
{
    assert(ctx != NULL);

    while (ctx->obj->data.length % align != 0) {
        if (!asm_data_push(ctx, 0, lineno))
            return false;
    }
    return true;
}

/* Append an integer little-endian. */
static bool asm_data_bits(AsmCtx *ctx, uint64_t bits, unsigned nbytes,
                          size_t lineno)
{
    assert(ctx != NULL);

    for (unsigned i = 0; i < nbytes; i++) {
        if (!asm_data_push(ctx, (uint8_t)(bits & 0xFFu), lineno))
            return false;
        bits >>= 8;
    }
    return true;
}

/* Parse an unsigned number with no trailing garbage. */
static bool asm_data_num(const char *tok, unsigned long long *num_out)
{
    assert(tok != NULL);
    assert(num_out != NULL);

    if (tok[0] == '-')
        return false;

    char *end = NULL;
    errno = 0;

    unsigned long long val = strtoull(tok, &end, 0);
    if (errno != 0 || end == tok)
        return false;

    end = (char *)asm_skip_ws(end);
    if (*end != '\0')
        return false;

    *num_out = val;
    return true;
}

/* Parse a double with no trailing garbage. */
static bool asm_data_double(const char *tok, double *num_out)
{
    assert(tok != NULL);
    assert(num_out != NULL);

    char *end = NULL;
    errno = 0;

    double val = strtod(tok, &end);
    if (errno != 0 || end == tok)
        return false;

    end = (char *)asm_skip_ws(end);
    if (*end != '\0')
        return false;

    *num_out = val;
    return true;
}

/* Data value handler, appends bytes and reports their data offset. */
typedef bool (*AsmDataHandler)(AsmCtx *ctx, const char *val, size_t *off_out,
                               size_t lineno);

/* Data type name and its handler. */
typedef struct {
    const char *name;
    AsmDataHandler handler;
} AsmDataType;

/* Integer entry of `nbytes` bytes, aligned to its size. */
static bool asm_data_int(AsmCtx *ctx, const char *type, const char *val,
                         unsigned nbytes, size_t *off_out, size_t lineno)
{
    assert(ctx != NULL);
    assert(type != NULL);
    assert(val != NULL);
    assert(off_out != NULL);

    if (!asm_data_align(ctx, nbytes, lineno))
        return false;

    unsigned long long num = 0;
    unsigned long long limit =
        nbytes >= 8 ? ULLONG_MAX : ((1ULL << (8 * nbytes)) - 1);

    if (!asm_data_num(val, &num) || num > limit) {
        LOG_E("line %zu: bad %s value '%s'", lineno, type, val);
        return false;
    }

    *off_out = ctx->obj->data.length;
    return asm_data_bits(ctx, (uint64_t)num, nbytes, lineno);
}

/* 64-bit integer entry, 8-byte aligned. */
static bool asm_data_u64(AsmCtx *ctx, const char *val, size_t *off_out,
                         size_t lineno)
{
    return asm_data_int(ctx, "u64", val, (unsigned)sizeof(uint64_t),
                        off_out, lineno);
}

/* 32-bit integer entry, 4-byte aligned. */
static bool asm_data_u32(AsmCtx *ctx, const char *val, size_t *off_out,
                         size_t lineno)
{
    return asm_data_int(ctx, "u32", val, (unsigned)sizeof(uint32_t),
                        off_out, lineno);
}

/* Double entry, 8-byte aligned. */
static bool asm_data_f64(AsmCtx *ctx, const char *val, size_t *off_out,
                         size_t lineno)
{
    assert(ctx != NULL);
    assert(val != NULL);
    assert(off_out != NULL);

    if (!asm_data_align(ctx, sizeof(double), lineno))
        return false;

    double num = 0;
    if (!asm_data_double(val, &num)) {
        LOG_E("line %zu: bad f64 value '%s'", lineno, val);
        return false;
    }

    union {
        double num;
        uint64_t bits;
    } pun = { .num = num };

    *off_out = ctx->obj->data.length;
    return asm_data_bits(ctx, pun.bits, (unsigned)sizeof(pun.bits), lineno);
}

/* Quoted string with `\n \\ \" \0 \t` escapes, exact bytes, no NUL added. */
static bool asm_data_str(AsmCtx *ctx, const char *val, size_t *off_out,
                         size_t lineno)
{
    assert(ctx != NULL);
    assert(val != NULL);
    assert(off_out != NULL);

    if (val[0] != '"') {
        LOG_E("line %zu: string needs opening quote", lineno);
        return false;
    }

    *off_out = ctx->obj->data.length;

    size_t i = 1;
    while (val[i] != '"') {
        if (val[i] == '\0') {
            LOG_E("line %zu: string needs closing quote", lineno);
            return false;
        }
        uint8_t byte = (uint8_t)val[i];
        if (val[i] == '\\') {
            i++;
            switch (val[i]) {
            case 'n':
                byte = (uint8_t)'\n';
                break;
            case 't':
                byte = (uint8_t)'\t';
                break;
            case 'r':
                byte = (uint8_t)'\r';
                break;
            case '0':
                byte = 0;
                break;
            case '\\':
                byte = (uint8_t)'\\';
                break;
            case '"':
                byte = (uint8_t)'"';
                break;
            default:
                LOG_E("line %zu: bad escape '\\%c'", lineno, val[i]);
                return false;
            }
        }
        if (!asm_data_push(ctx, byte, lineno))
            return false;
        i++;
    }

    if (*asm_skip_ws(val + i + 1) != '\0') {
        LOG_E("line %zu: trailing text after string", lineno);
        return false;
    }
    return true;
}

/* Hex digit value, -1 for anything else. */
static int asm_data_hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Raw bytes as hex pairs, spaces allowed between them. */
static bool asm_data_bytes(AsmCtx *ctx, const char *val, size_t *off_out,
                           size_t lineno)
{
    assert(ctx != NULL);
    assert(val != NULL);
    assert(off_out != NULL);

    *off_out = ctx->obj->data.length;

    const char *pos = val;
    while (*pos != '\0') {
        if (asm_is_ws(*pos)) {
            pos++;
            continue;
        }

        int hi = asm_data_hex(pos[0]);
        int lo = asm_data_hex(pos[1]);
        if (hi < 0 || lo < 0) {
            LOG_E("line %zu: bad hex byte near '%s'", lineno, pos);
            return false;
        }

        if (!asm_data_push(ctx, (uint8_t)((hi << 4) | lo), lineno))
            return false;
        pos += 2;
    }

    return true;
}

/* Source data types. */
static const AsmDataType asm_data_types[] = {
    { "u64",   asm_data_u64   },
    { "u32",   asm_data_u32   },
    { "f64",   asm_data_f64   },
    { "str",   asm_data_str   },
    { "bytes", asm_data_bytes },
};

/* Data type row by source name, NULL when unknown. */
static const AsmDataType *asm_data_type_find(const char *name)
{
    assert(name != NULL);

    for (size_t i = 0; i < ARR_LEN(asm_data_types); i++) {
        if (strcmp(name, asm_data_types[i].name) == 0)
            return &asm_data_types[i];
    }

    return NULL;
}

bool asm_data_entry(AsmCtx *ctx, char *line, size_t lineno)
{
    assert(ctx != NULL);
    assert(line != NULL);

    char *colon = strchr(line, ':');
    if (colon == NULL) {
        LOG_E("line %zu: data entry needs 'name: <type> <value>'", lineno);
        return false;
    }
    *colon = '\0';

    char *name = (char *)asm_skip_ws(line);
    char *name_end = name + strlen(name);

    while (name_end > name && asm_is_ws(name_end[-1]))
        *--name_end = '\0';

    if (!asm_valid_name(name)) {
        LOG_E("line %zu: bad data name", lineno);
        return false;
    }

    char *type = (char *)asm_skip_ws(colon + 1);
    size_t type_len = strcspn(type, " \t\r");

    char *val = (char *)asm_skip_ws(type + type_len);
    if (type_len == 0 || *val == '\0') {
        LOG_E("line %zu: data entry needs '<type> <value>'", lineno);
        return false;
    }
    type[type_len] = '\0';

    const AsmDataType *data_type = asm_data_type_find(type);
    if (data_type == NULL) {
        LOG_E("line %zu: unknown data type '%s' (u64, u32, f64, str, bytes)",
              lineno, type);
        return false;
    }

    size_t offset = 0;
    if (!data_type->handler(ctx, val, &offset, lineno))
        return false;

    return asm_sym_add(ctx, name, offset, ASM_SYM_DATA, lineno);
}
