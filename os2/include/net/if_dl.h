/* Public domain. No rights reserved. Wholly AI-authored. */
/* TCP/IP link-level socket addresses. The OS2_ prefix keeps these apart from the host's BSD names. */

#ifndef _INCL_NET_IF_DL_H_
#define _INCL_NET_IF_DL_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

#pragma pack(push, 1)

    typedef struct OS2_sockaddr_dl
    {
        uint8_t sdl_len;
        uint8_t sdl_family;
        uint16_t sdl_index;
        uint8_t sdl_type;
        uint8_t sdl_nlen;
        uint8_t sdl_alen;
        uint8_t sdl_slen;
        char sdl_data[12];
    } OS2_sockaddr_dl;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
