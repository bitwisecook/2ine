/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TB_PACK_H
#define TB_PACK_H
#include <stddef.h>
/* Decodes an IBM PACK member (Deark dskdcmps), or a PACK2 member (Deark FTCOMP) when
   ftcomp is set. Allocates exactly expected bytes, or fails without returning partial
   output. */
int tb_pack_decode(const void *input, size_t size, size_t expected, int ftcomp,
                   unsigned char **output);
#endif
