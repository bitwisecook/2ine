/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_MSG_H_
#define _INCL_MSG_H_

#include "os2types.h"

#ifdef __cplusplus
extern "C" {
#endif

OS2EXPORT APIRET16 OS2API16 Dos16PutMessage(USHORT hfile, USHORT cbMsg, PCHAR pBuf) OS2APIINFO(ord=1,name=DOSPUTMESSAGE);
OS2EXPORT APIRET16 OS2API16 Dos16TrueGetMessage(PVOID pTable, USHORT cTable, PCHAR pData, USHORT cbBuf, USHORT msgnum, PCHAR pFilename, PUSHORT pcbMsg, PVOID msgseg) OS2APIINFO(ord=2,name=DOSTRUEGETMESSAGE);
OS2EXPORT APIRET OS2API DosPutMessage(HFILE hfile, ULONG cbMsg, PCHAR pBuf) OS2APIINFO(5);
// MSG ordinal 6 forwards to DOSCALL1's Dos32TrueGetMessage, a kernel thunk
//  entered through the linker's MSGSEG32 16:32 call thunk. That thunk pushes
//  one hidden 32-bit frame word and the real entry point removes it with
//  `ret 4`, so this entry is stdcall with the hidden word as its formal
//  argument; the seven documented arguments follow it on the stack.
OS2EXPORT __attribute__((stdcall)) APIRET OS2API DosTrueGetMessage(PVOID msgseg) OS2APIINFO(6);

#ifdef __cplusplus
}
#endif

#endif

// end of msg.h ...

