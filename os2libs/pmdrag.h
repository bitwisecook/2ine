/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OS2LIBS_PMDRAG_H
#define TWOINE_OS2LIBS_PMDRAG_H
#include "os2types.h"
#include "os2libs/pmwin.h"
OS2EXPORT ULONG OS2API DrgAddStrHandle(PSZ name) OS2APIINFO(33);
OS2EXPORT BOOL OS2API DrgDeleteStrHandle(ULONG string) OS2APIINFO(37);
OS2EXPORT ULONG OS2API DrgQueryStrName(ULONG string, ULONG capacity, PSZ output) OS2APIINFO(49);
OS2EXPORT ULONG OS2API DrgQueryStrNameLen(ULONG string) OS2APIINFO(50);
OS2EXPORT BOOL OS2API DrgAccessDraginfo(ULONG address) OS2APIINFO(32);
OS2EXPORT ULONG OS2API DrgAllocDraginfo(ULONG count) OS2APIINFO(34);
OS2EXPORT ULONG OS2API DrgAllocDragtransfer(ULONG count) OS2APIINFO(35);
OS2EXPORT BOOL OS2API DrgDeleteDraginfoStrHandles(ULONG address) OS2APIINFO(36);
OS2EXPORT HWND OS2API DrgDrag(HWND source, ULONG info, ULONG images, ULONG count, LONG terminate,
                              ULONG reserved) OS2APIINFO(38);
OS2EXPORT BOOL OS2API DrgFreeDraginfo(ULONG address) OS2APIINFO(39);
OS2EXPORT BOOL OS2API DrgFreeDragtransfer(ULONG address) OS2APIINFO(40);
OS2EXPORT HPS OS2API DrgGetPS(HWND window) OS2APIINFO(41);
OS2EXPORT BOOL OS2API DrgPostTransferMsg(HWND window, ULONG message, ULONG transfer, ULONG flags,
                                         ULONG reserved, BOOL retry) OS2APIINFO(42);
OS2EXPORT ULONG OS2API DrgQueryDragitemCount(ULONG address) OS2APIINFO(45);
OS2EXPORT ULONG OS2API DrgQueryDragitemPtr(ULONG address, ULONG index) OS2APIINFO(46);
OS2EXPORT BOOL OS2API DrgReleasePS(HPS space) OS2APIINFO(53);
OS2EXPORT MRESULT OS2API DrgSendTransferMsg(HWND window, ULONG message, MPARAM first, MPARAM second)
    OS2APIINFO(54);
OS2EXPORT BOOL OS2API DrgSetDragitem(ULONG address, ULONG item, ULONG bytes, ULONG index)
    OS2APIINFO(57);
OS2EXPORT BOOL OS2API DrgVerifyRMF(ULONG item_address, PSZ mechanism, PSZ format) OS2APIINFO(59);
#endif
