#include "asm_priv.h"
#include "toys/debug.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


static bool asm_data_push(AsmCtx *ctx, uint8_t byte, size_t lineno)
{
    assert(ctx != NULL);

    if (wst_vec_u8_push(&ctx->obj->data, byte) != WST_VEC_NO_ERR) {
        LOG_E("line %zu: out of memory", lineno);
        return false;
    }
    return true;
}

static bool asm_data_align(AsmCtx *ctx, size_t align, size_t lineno)
{
    assert(ctx != NULL);

    while (ctx->obj->data.length % align != 0) {
        if (!asm_data_push(ctx, 0, lineno))
            return false;
    }
    return true;
}

/* Write `bits` little-endian, used by u64/u32 entries. */
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

/* Unsigned number, no trailing garbage, `-` rejected up front. */
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

/* `name: <type> <value>` in `section .data`, address is the data offset. */
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
    char *nend = name + strlen(name);
    while (nend > name && asm_is_ws(nend[-1]))
        *--nend = '\0';
    if (!asm_valid_name(name)) {
        LOG_E("line %zu: bad data name", lineno);
        return false;
    }

    char *type = (char *)asm_skip_ws(colon + 1);
    size_t tlen = strcspn(type, " \t\r");
    char *val = (char *)asm_skip_ws(type + tlen);
    if (tlen == 0 || *val == '\0') {
        LOG_E("line %zu: data entry needs '<type> <value>'", lineno);
        return false;
    }
    type[tlen] = '\0';

    size_t off = 0;
    if (strcmp(type, "u64") == 0 || strcmp(type, "f64") == 0) {
        if (!asm_data_align(ctx, 8, lineno))
            return false;
        off = ctx->obj->data.length;
        uint64_t bits = 0;
        if (type[0] == 'u') {
            unsigned long long num = 0;
            if (!asm_data_num(val, &num)) {
                LOG_E("line %zu: bad u64 value", lineno);
                return false;
            }
            bits = (uint64_t)num;
        } else {
            double num = 0;
            if (!asm_data_double(val, &num)) {
                LOG_E("line %zu: bad f64 value", lineno);
                return false;
            }
            memcpy(&bits, &num, sizeof(bits));
        }
        if (!asm_data_bits(ctx, bits, 8, lineno))
            return false;
    } else if (strcmp(type, "u32") == 0) {
        if (!asm_data_align(ctx, 4, lineno))
            return false;
        off = ctx->obj->data.length;
        unsigned long long num = 0;
        if (!asm_data_num(val, &num) || num > 0xFFFFFFFFu) {
            LOG_E("line %zu: bad u32 value", lineno);
            return false;
        }
        if (!asm_data_bits(ctx, (uint64_t)num, 4, lineno))
            return false;
    } else if (strcmp(type, "str") == 0) {
        off = ctx->obj->data.length;
        if (!asm_data_str(ctx, val, lineno))
            return false;
    } else {
        LOG_E("line %zu: unknown data type '%s' (u64, u32, f64, str)",
              lineno, type);
        return false;
    }

    return asm_sym_add(ctx, name, off, ASM_SYM_DATA, lineno);
}
