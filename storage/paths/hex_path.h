/* Public domain. No rights reserved. Wholly AI-authored. */
/* Lowercase hexadecimal path components, so a host path or member name of any spelling
   becomes one virtual-path component. */
#ifndef TB_STORAGE_HEX_PATH_H
#define TB_STORAGE_HEX_PATH_H
#include <stddef.h>

/* Returns an owned prefix followed by text as lowercase hex, or NULL with errno
   (ENAMETOOLONG when the result cannot be sized). */
char *tb_hex_path_encode(const char *prefix, const char *text);
/* Returns length hex digits decoded as an owned string ("" when length is 0), or NULL
   with errno EINVAL for an odd length, a digit outside 0-9a-f, or an encoded NUL byte. */
char *tb_hex_path_decode(const char *hex, size_t length);
#endif
