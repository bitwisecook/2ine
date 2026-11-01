/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OPEN32_INPUT_H
#define TWOINE_OPEN32_INPUT_H
#include "os2libs/pmwin.h"
typedef struct Open32Key
{
    ULONG message, key, parameter, character, flags;
    BOOL text;
} Open32Key;
void open32DecodeKey(ULONG flags, ULONG data, Open32Key *key);
void open32UpdateKey(const Open32Key *key);
BOOL open32PmMessage(const QMSG *native, ULONG *message, ULONG *first, ULONG *second);
BOOL open32QueuedCallback(ULONG window, ULONG message, ULONG *result);
void open32QueueCleanup(void);
void open32QueueWindowDestroyed(ULONG window);
void open32ValidateQueuedPaint(ULONG window);
#endif
