/* Public domain. No rights reserved. Wholly AI-authored. */
#include "storage/archives/ibm_pack/decode.h"
#include "storage/archives/ibm_pack/deark_shim.h"
#include "third_party/deark/ftcomp.inc"
#include "third_party/deark/dskdcmps.h"

int tb_pack_decode(const void *input, size_t size, size_t expected, int ftcomp,
                   unsigned char **output)
{
    *output = NULL;
    if (!expected || expected > TB_PACK_LIMIT || size > TB_PACK_LIMIT)
        return 0;
    deark *c = calloc(1, sizeof *c);
    if (!c)
        return 0;
    int ok = 0;
    if (!setjmp(c->escape))
    {
        dbuf in = {.c = c, .data = (u8 *)input, .len = (i64)size, .capacity = (i64)size};
        dbuf *out = dbuf_create_membuf(c, (i64)expected, 0);
        struct de_dfilter_in_params params = {.f = &in, .len = (i64)size};
        struct de_dfilter_out_params result = {.f = out, .expected_len = (i64)expected};
        struct de_dfilter_results status = {0};
        if (ftcomp)
            fmtutil_ftcomp_codectype1(c, &params, &result, &status, NULL);
        else
            dskdcmps_run(c, &params, &result, &status);
        if (!status.errcode && out->len == (i64)expected)
        {
            *output = malloc(expected);
            if (*output)
            {
                memcpy(*output, out->data, expected);
                ok = 1;
            }
        }
    }
    while (c->allocations)
    {
        Allocation *a = c->allocations;
        c->allocations = a->next;
        free(a);
    }
    free(c);
    return ok;
}
