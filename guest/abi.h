/* Public domain. No rights reserved. Wholly AI-authored. */
/* Fixed OS/2 process/thread wire layouts. Address fields contain guest values. */
#ifndef TWOINE_GUEST_ABI_H
#define TWOINE_GUEST_ABI_H
#include <stdint.h>
#include <stddef.h>

typedef struct LxGuestTib
{
    uint32_t exception_chain;
    uint32_t stack_base;
    uint32_t stack_limit;
    uint32_t tib2;
    uint32_t version;
    uint32_t ordinal;
} LxGuestTib;
typedef struct LxGuestTib2
{
    uint32_t thread_id;
    uint32_t priority;
    uint32_t version;
    uint16_t must_complete;
    uint16_t force_flag;
} LxGuestTib2;
typedef struct LxGuestPib
{
    uint32_t process_id;
    uint32_t parent_id;
    uint32_t module;
    uint32_t command;
    uint32_t environment;
    uint32_t status;
    uint32_t type;
} LxGuestPib;
/* The numeric C fields describe positions. Encode/decode little-endian values
   explicitly, including on a host whose integer byte order differs from x86. */
static inline uint32_t lx_guest_load_u32(const void *data)
{
    const uint8_t *p = data;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void lx_guest_store_u32(void *data, uint32_t value)
{
    uint8_t *p = data;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}
static inline uint16_t lx_guest_load_u16(const void *data)
{
    const uint8_t *p = data;
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static inline void lx_guest_store_u16(void *data, uint16_t value)
{
    uint8_t *p = data;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
typedef char LxGuestTibSizeCheck[sizeof(LxGuestTib) == 24 ? 1 : -1];
typedef char LxGuestTib2SizeCheck[sizeof(LxGuestTib2) == 16 ? 1 : -1];
typedef char LxGuestPibSizeCheck[sizeof(LxGuestPib) == 28 ? 1 : -1];
typedef char LxGuestTibLinkCheck[offsetof(LxGuestTib, tib2) == 12 ? 1 : -1];
#endif
