/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TB_BASE64_H
#define TB_BASE64_H
#include <stddef.h>
#include <stdint.h>
/* RFC 4648 base64 with padding, as an owned string; NULL above 64 MiB of input. */
char *tb_base64(const uint8_t *bytes, size_t size);
#endif
