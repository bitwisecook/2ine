/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest entry may arrive with the i386 OS/2 four-byte stack alignment. */
#ifndef TWOINE_HOST_ABI_H
#define TWOINE_HOST_ABI_H
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define LX_HOST_API_ENTRY __attribute__((force_align_arg_pointer))
#else
#define LX_HOST_API_ENTRY
#endif
#endif
