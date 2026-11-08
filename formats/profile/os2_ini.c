/* Public domain. No rights reserved. Wholly AI-authored. */
#include "formats/profile/os2_ini.h"
#include "formats/endian.h"
#include <stdlib.h>
#include <string.h>

/* Native header fields. */
#define INI_FIRST_APPLICATION 4
#define INI_TOTAL_SIZE 8
/* Application record fields. */
#define APPLICATION_NEXT 0
#define APPLICATION_FIRST_KEY 4
#define APPLICATION_NAME_LENGTH 12
#define APPLICATION_NAME 16
/* Key record fields. */
#define KEY_NEXT 0
#define KEY_NAME_LENGTH 8
#define KEY_NAME 12
#define KEY_DATA_LENGTH 16
#define KEY_DATA 20

int tb_profile_recognised(const unsigned char *p, size_t n)
{
    return (n >= TB_PROFILE_MAGIC_SIZE && !memcmp(p, TB_PROFILE_MAGIC, TB_PROFILE_MAGIC_SIZE)) ||
           (n >= 4 && tb_le32(p) == TB_OS2_INI_SIGNATURE);
}

static int native_string(const unsigned char *p, size_t n, uint32_t at, uint32_t size)
{
    return size && at <= n && size <= n - at && !p[at + size - 1] && !memchr(p + at, 0, size - 1);
}

/* Each record is visited once, so a cyclic chain is rejected rather than followed. */
static int mark(unsigned char *seen, size_t n, uint32_t at, uint32_t bytes)
{
    if (at < TB_OS2_INI_HEADER_SIZE || at > n || bytes > n - at ||
        (seen[at / 8] & (1u << (at % 8))))
        return 0;
    seen[at / 8] |= 1u << (at % 8);
    return 1;
}

static int native_walk(const unsigned char *p, size_t n, TbProfileVisitor visit, void *context)
{
    unsigned char *seen = calloc((n + 7) / 8, 1);
    int ok = seen && tb_le32(p + INI_TOTAL_SIZE) <= n;
    uint32_t app = ok ? tb_le32(p + INI_FIRST_APPLICATION) : 0;
    while (ok && app)
    {
        if (!mark(seen, n, app, TB_OS2_INI_APPLICATION_SIZE))
        {
            ok = 0;
            break;
        }
        uint32_t name = tb_le32(p + app + APPLICATION_NAME),
                 a = tb_le16(p + app + APPLICATION_NAME_LENGTH);
        if (!native_string(p, n, name, a))
        {
            ok = 0;
            break;
        }
        uint32_t key = tb_le32(p + app + APPLICATION_FIRST_KEY);
        while (ok && key)
        {
            if (!mark(seen, n, key, TB_OS2_INI_KEY_SIZE))
            {
                ok = 0;
                break;
            }
            uint32_t kn = tb_le32(p + key + KEY_NAME), k = tb_le16(p + key + KEY_NAME_LENGTH);
            uint32_t data = tb_le32(p + key + KEY_DATA), bytes = tb_le16(p + key + KEY_DATA_LENGTH);
            ok = native_string(p, n, kn, k) && data <= n && bytes <= n - data &&
                 visit(context, p + name, a, p + kn, k, p + data, bytes);
            key = tb_le32(p + key + KEY_NEXT);
        }
        app = tb_le32(p + app + APPLICATION_NEXT);
    }
    free(seen);
    return ok;
}

int tb_profile_walk(const unsigned char *p, size_t n, TbProfileVisitor visit, void *context)
{
    if (!n)
        return 1;
    if (n >= TB_OS2_INI_HEADER_SIZE && tb_le32(p) == TB_OS2_INI_SIGNATURE)
        return native_walk(p, n, visit, context);
    if (n < TB_PROFILE_HEADER_SIZE || memcmp(p, TB_PROFILE_MAGIC, TB_PROFILE_MAGIC_SIZE))
        return 0;
    uint32_t count = tb_le32(p + TB_PROFILE_MAGIC_SIZE);
    size_t at = TB_PROFILE_HEADER_SIZE;
    while (count--)
    {
        if (n - at < TB_PROFILE_ENTRY_HEADER_SIZE)
            return 0;
        uint32_t a = tb_le32(p + at), k = tb_le32(p + at + 4), bytes = tb_le32(p + at + 8);
        at += TB_PROFILE_ENTRY_HEADER_SIZE;
        if (!a || !k || a > TB_PROFILE_NAME_LIMIT || k > TB_PROFILE_NAME_LIMIT || a > n - at ||
            k > n - at - a || bytes > n - at - a - k ||
            !visit(context, p + at, a, p + at + a, k, p + at + a + k, bytes))
            return 0;
        at += a + k + bytes;
    }
    return at == n;
}

size_t tb_profile_entry_size(size_t application_length, size_t key_length, size_t data_size)
{
    return TB_PROFILE_ENTRY_HEADER_SIZE + application_length + 1 + key_length + 1 + data_size;
}

void tb_profile_put_header(unsigned char *out, uint32_t count)
{
    memcpy(out, TB_PROFILE_MAGIC, TB_PROFILE_MAGIC_SIZE);
    tb_put_le32(out + TB_PROFILE_MAGIC_SIZE, count);
}

size_t tb_profile_put_entry(unsigned char *out, const char *application, size_t a, const char *key,
                            size_t k, const unsigned char *data, size_t n)
{
    tb_put_le32(out, (uint32_t)a + 1);
    tb_put_le32(out + 4, (uint32_t)k + 1);
    tb_put_le32(out + 8, (uint32_t)n);
    size_t at = TB_PROFILE_ENTRY_HEADER_SIZE;
    memcpy(out + at, application, a);
    out[at + a] = 0;
    at += a + 1;
    memcpy(out + at, key, k);
    out[at + k] = 0;
    at += k + 1;
    memcpy(out + at, data, n);
    return at + n;
}
