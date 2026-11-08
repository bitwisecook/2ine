/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OS2LIBS_PMDRAG_H
#define TWOINE_OS2LIBS_PMDRAG_H
#include "os2/include/os2types.h"
#include "os2/include/pmwin.h"
OS2EXPORT ULONG OS2API DrgAddStrHandle(PSZ name);
OS2EXPORT BOOL OS2API DrgDeleteStrHandle(ULONG string);
OS2EXPORT ULONG OS2API DrgQueryStrName(ULONG string, ULONG capacity, PSZ output);
OS2EXPORT ULONG OS2API DrgQueryStrNameLen(ULONG string);
OS2EXPORT BOOL OS2API DrgAccessDraginfo(ULONG address);
OS2EXPORT ULONG OS2API DrgAllocDraginfo(ULONG count);
OS2EXPORT ULONG OS2API DrgAllocDragtransfer(ULONG count);
OS2EXPORT BOOL OS2API DrgDeleteDraginfoStrHandles(ULONG address);
OS2EXPORT HWND OS2API DrgDrag(HWND source, ULONG info, ULONG images, ULONG count, LONG terminate,
                              ULONG reserved);
OS2EXPORT BOOL OS2API DrgFreeDraginfo(ULONG address);
OS2EXPORT BOOL OS2API DrgFreeDragtransfer(ULONG address);
OS2EXPORT HPS OS2API DrgGetPS(HWND window);
OS2EXPORT BOOL OS2API DrgPostTransferMsg(HWND window, ULONG message, ULONG transfer, ULONG flags,
                                         ULONG reserved, BOOL retry);
OS2EXPORT ULONG OS2API DrgQueryDragitemCount(ULONG address);
OS2EXPORT ULONG OS2API DrgQueryDragitemPtr(ULONG address, ULONG index);
OS2EXPORT BOOL OS2API DrgReleasePS(HPS space);
OS2EXPORT MRESULT OS2API DrgSendTransferMsg(HWND window, ULONG message, MPARAM first,
                                            MPARAM second);
OS2EXPORT BOOL OS2API DrgSetDragitem(ULONG address, ULONG item, ULONG bytes, ULONG index);
OS2EXPORT BOOL OS2API DrgVerifyRMF(ULONG item_address, PSZ mechanism, PSZ format);
#endif
