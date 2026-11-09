/* Public domain. No rights reserved. Wholly AI-authored. */
/* OS/2 profile (INI) files: the native OS/2 binary layout, read only, and the
   2ine "2INEPRF1" layout, read and written. Both hold application/key/data
   triples; application and key names are Latin-1 and NUL-terminated. */
#ifndef TB_OS2_INI_H
#define TB_OS2_INI_H
#include <stddef.h>
#include <stdint.h>

/* The largest profile file the readers accept. */
#define TB_PROFILE_SIZE_LIMIT (32u * 1024u * 1024u)

/* Native OS/2 profiles start with this 32-bit little-endian signature. */
#define TB_OS2_INI_SIGNATURE UINT32_MAX
#define TB_OS2_INI_HEADER_SIZE 20u
#define TB_OS2_INI_APPLICATION_SIZE 20u
#define TB_OS2_INI_KEY_SIZE 24u

/* 2ine profiles: magic, little-endian entry count, then entries. */
#define TB_PROFILE_MAGIC "2INEPRF1"
#define TB_PROFILE_MAGIC_SIZE 8u
#define TB_PROFILE_HEADER_SIZE 12u
#define TB_PROFILE_ENTRY_HEADER_SIZE 12u
#define TB_PROFILE_NAME_LIMIT 65536u

/* Name sizes include the terminating NUL. Returning 0 stops the walk. */
typedef int (*TbProfileVisitor)(void *context, const unsigned char *application,
                                size_t application_size, const unsigned char *key, size_t key_size,
                                const unsigned char *data, size_t data_size);

/* Either layout, by signature. */
int tb_profile_recognised(const unsigned char *bytes, size_t size);
/* Visits every entry in file order. An empty file is a valid empty profile.
   Returns 0 when the layout is invalid or the visitor stops. */
int tb_profile_walk(const unsigned char *bytes, size_t size, TbProfileVisitor visit, void *context);
/* Writes a 2ine profile. Names exclude their NUL; the encoder appends it. */
size_t tb_profile_entry_size(size_t application_length, size_t key_length, size_t data_size);
void tb_profile_put_header(unsigned char *out, uint32_t count);
size_t tb_profile_put_entry(unsigned char *out, const char *application, size_t application_length,
                            const char *key, size_t key_length, const unsigned char *data,
                            size_t data_size);
#endif
