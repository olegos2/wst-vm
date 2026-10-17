#include "asm_priv.h"
#include "toys/debug.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

/* Quoted string with `\n \\ \" \0 \t` escapes, exact bytes, no NUL added. */
static bool asm_data_str(AsmCtx *ctx, const char *val, size_t lineno)
{
    assert(ctx != NULL);
    assert(val != NULL);

    if (val[0] != '"') {
        LOG_E("line %zu: string needs opening quote", lineno);
        return false;
    }

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

    size_t offset = 0;

    if (strcmp(type, "u64") == 0) {
        if (!asm_data_int(ctx, type, val, 8, &offset, lineno))
            return false;
    }
    else if (strcmp(type, "u32") == 0) {
        if (!asm_data_int(ctx, type, val, 4, &offset, lineno))
            return false;
    }
    else if (strcmp(type, "f64") == 0) {
        if (!asm_data_align(ctx, 8, lineno))
            return false;

        offset = ctx->obj->data.length;

        double num = 0;
        if (!asm_data_double(val, &num)) {
            LOG_E("line %zu: bad f64 value '%s'", lineno, val);
            return false;
        }

        uint64_t bits = 0;
        memcpy(&bits, &num, sizeof(bits));
        if (!asm_data_bits(ctx, bits, 8, lineno))
            return false;
    }
    else if (strcmp(type, "str") == 0) {
        offset = ctx->obj->data.length;

        if (!asm_data_str(ctx, val, lineno))
            return false;
    }
    else {
        LOG_E("line %zu: unknown data type '%s' (u64, u32, f64, str)",
              lineno, type);
        return false;
    }

    return asm_sym_add(ctx, name, offset, ASM_SYM_DATA, lineno);
}
