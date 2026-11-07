/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OPEN32_BATCH_SERVICES_H
#define TWOINE_OPEN32_BATCH_SERVICES_H
#include "os2libs/pmwinx.h"
ULONG AddAtom(PSZ name);
BOOL AdjustWindowRect(PVOID rectangle, ULONG style, BOOL menu);
BOOL Beep(ULONG frequency, ULONG duration);
ULONG BeginDeferWindowPos(LONG count);
BOOL CheckRadioButton(ULONG dialog, LONG first, LONG last, LONG checked);
ULONG ChildWindowFromPoint(ULONG parent, LONG x, LONG y);
BOOL ChooseColor(PVOID record);
BOOL ChooseFont(PVOID record);
BOOL ClipCursor(PVOID rectangle);
LONG CompareFileTime(PVOID first, PVOID second);
BOOL CopyFile(PSZ source, PSZ destination, BOOL exclusive);
ULONG CreateDialog(ULONG module, PSZ resource, ULONG owner, ULONG procedure);
ULONG CreateDialogParam(ULONG module, PSZ resource, ULONG owner, ULONG procedure, ULONG parameter);
BOOL CreateDirectory(PSZ path, PVOID security);
ULONG CreateEvent(PVOID security, BOOL manual, BOOL initial, PSZ name);
ULONG CreateMutex(PVOID security, BOOL owner, PSZ name);
BOOL DdeAbandonTransaction(ULONG id, ULONG handle, ULONG transaction);
ULONG DdeAccessData(ULONG handle, PVOID size);
ULONG DdeAddData(ULONG handle, PVOID input, ULONG count, ULONG offset);
ULONG DdeClientTransaction(ULONG address, ULONG size, ULONG handle, ULONG item, ULONG format,
                           ULONG type, ULONG timeout, PVOID status);
ULONG DdeConnect(ULONG id, ULONG service, ULONG topic, PVOID context);
ULONG DdeCreateDataHandle(ULONG id, PVOID input, ULONG count, ULONG offset, ULONG item,
                          ULONG format, ULONG flags);
BOOL DdeDisconnect(ULONG handle);
BOOL DdeFreeDataHandle(ULONG handle);
ULONG DdeGetData(ULONG handle, PVOID output, ULONG capacity, ULONG offset);
BOOL DdePostAdvise(ULONG id, ULONG topic, ULONG item);
ULONG DdeQueryConvInfo(ULONG handle, ULONG transaction, PVOID output);
BOOL DdeSetUserHandle(ULONG handle, ULONG transaction, ULONG user);
BOOL DdeUnaccessData(ULONG handle);
LONG DefDlgProc(ULONG window, ULONG message, ULONG first, ULONG second);
ULONG DeferWindowPos(ULONG handle, ULONG window, ULONG after, LONG x, LONG y, LONG width,
                     LONG height, ULONG flags);
ULONG DeleteAtom(ULONG atom);
BOOL DeleteFile(PSZ path);
BOOL DlgDirSelectEx(ULONG dialog, PSZ output, LONG capacity, LONG id);
BOOL DosDateTimeToFileTime(USHORT date, USHORT time, PVOID output);
VOID DragAcceptFiles(ULONG window, BOOL accept);
VOID DragFinish(ULONG drop);
ULONG DragQueryFile(ULONG drop, ULONG index, PSZ output, ULONG capacity);
BOOL DragQueryPoint(ULONG drop, PVOID output);
BOOL EndDeferWindowPos(ULONG handle);
BOOL EnumThreadWindows(ULONG thread, ULONG procedure, ULONG parameter);
BOOL EnumWindows(ULONG procedure, ULONG parameter);
BOOL FileTimeToDosDateTime(PVOID input, PVOID date, PVOID time);
ULONG FindAtom(PSZ name);
ULONG FindWindow(PSZ class_name, PSZ title);
ULONG GetCursor(void);
BOOL GetDiskFreeSpace(PSZ root, PVOID sectors, PVOID bytes, PVOID free_clusters,
                      PVOID total_clusters);
LONG GetDlgCtrlID(ULONG window);
ULONG GetDlgItemInt(ULONG dialog, LONG id, PVOID translated, BOOL sign);
ULONG GetDoubleClickTime(void);
ULONG GetEnvironmentStrings(void);
ULONG GetEnvironmentVariable(PSZ name, PSZ output, ULONG capacity);
BOOL GetFileTime(ULONG handle, PVOID creation, PVOID access, PVOID write);
SHORT GetFileTitle(PSZ path, PSZ output, USHORT capacity);
ULONG GetForegroundWindow(void);
LONG GetKeyNameText(ULONG parameter, PSZ output, LONG capacity);
ULONG GetLastActivePopup(ULONG window);
ULONG GetNextDlgTabItem(ULONG dialog, ULONG control, BOOL previous);
ULONG GetNextWindow(ULONG window, ULONG direction);
LONG GetPriorityClipboardFormat(PVOID formats, LONG count);
ULONG GetPrivateProfileInt(PSZ section, PSZ key, LONG fallback, PSZ filename);
ULONG GetSystemDirectory(PSZ output, ULONG capacity);
ULONG GetTempFileName(PSZ path, PSZ prefix, ULONG unique, PSZ output);
ULONG GetTempPath(ULONG capacity, PSZ output);
ULONG GetTimeZoneInformation(PVOID output);
ULONG GetTopWindow(ULONG parent);
LONG GetUpdateRgn(ULONG window, ULONG output, BOOL erase);
USHORT GetWindowWord(ULONG handle, LONG index);
VOID GlobalMemoryStatus(PVOID output);
LONG InterlockedDecrement(PLONG value);
LONG InterlockedIncrement(PLONG value);
BOOL IsBadHugeReadPtr(PVOID pointer, ULONG size);
BOOL IsBadHugeWritePtr(PVOID pointer, ULONG size);
BOOL IsBadStringPtr(ULONG address, ULONG maximum);
BOOL IsCharAlpha(ULONG character);
BOOL IsCharAlphaNumeric(ULONG character);
BOOL IsCharUpper(ULONG character);
BOOL IsChild(ULONG parent, ULONG child);
BOOL IsDialogMessage(ULONG dialog, PVOID message);
ULONG LoadMenu(ULONG module, PSZ resource);
BOOL LockWindowUpdate(ULONG window);
BOOL MapDialogRect(ULONG dialog, PVOID rectangle);
LONG MapWindowPoints(ULONG from, ULONG to, PVOID points, ULONG count);
LONG MessageBox(ULONG owner, PSZ text, PSZ caption, ULONG flags);
BOOL ModifyMenu(ULONG handle, ULONG identity, ULONG flags, ULONG identifier, PSZ text);
BOOL MoveFile(PSZ source, PSZ destination);
VOID OutputDebugString(PSZ text);
BOOL PostThreadMessage(ULONG thread, ULONG message, ULONG first, ULONG second);
LONG RegQueryInfoKey(ULONG key, PSZ class_name, PULONG class_size, PULONG reserved, PULONG subkeys,
                     PULONG max_subkey, PULONG max_class, PULONG values, PULONG max_value_name,
                     PULONG max_value_data, PULONG security, PVOID last_write);
LONG RegQueryValue(ULONG key, PSZ subkey, PSZ output, PULONG size);
LONG RegSetValue(ULONG key, PSZ subkey, ULONG type, PVOID data, ULONG size);
BOOL ReleaseMutex(ULONG handle);
BOOL RemoveDirectory(PSZ path);
BOOL RemoveFontResource(PSZ path);
BOOL ReplyMessage(LONG result);
BOOL ResetEvent(ULONG handle);
ULONG SearchPath(PSZ path, PSZ file, PSZ extension, ULONG capacity, PSZ output, PVOID part);
BOOL SetDlgItemInt(ULONG dialog, LONG id, ULONG value, BOOL sign);
BOOL SetEnvironmentVariable(PSZ name, PSZ value);
BOOL SetEvent(ULONG handle);
BOOL SetFileAttributes(PSZ path, ULONG attributes);
BOOL SetFileTime(ULONG handle, PVOID creation, PVOID access, PVOID write);
ULONG SetHandleCount(ULONG count);
USHORT SetWindowWord(ULONG handle, LONG index, USHORT value);
LONG ShowCursor(BOOL show);
BOOL TrackPopupMenu(ULONG handle, ULONG flags, LONG x, LONG y, LONG reserved, ULONG owner,
                    PVOID exclude);
BOOL TranslateMDISysAccel(ULONG client, PVOID message);
SHORT VkKeyScan(ULONG character);
BOOL WriteProfileString(PSZ section, PSZ key, PSZ value);
VOID ZeroMemory(PVOID output, ULONG count);
LONG _lcreat(PSZ path, LONG attributes);
LONG _lopen(PSZ path, LONG flags);
ULONG MapVirtualKey(ULONG code, ULONG kind);
ULONG GetMenuContextHelpId(ULONG handle);
BOOL SetMenuContextHelpId(ULONG handle, ULONG help);
ULONG ExtractIcon(ULONG instance, PSZ path, ULONG index);
LONG lstrcmp2(PSZ first, PSZ second);
ULONG GetKeyboardLayout(ULONG thread);
BOOL GetStringTypeEx(ULONG locale, ULONG kind, PSZ input, LONG count, PVOID output);
ULONG _CreateDialogIndirectParam(ULONG module, ULONG address, ULONG owner, ULONG procedure,
                                 ULONG parameter);
LONG _DialogBoxIndirect(ULONG module, ULONG address, ULONG owner, ULONG procedure);
LONG _DialogBoxIndirectParam(ULONG module, ULONG address, ULONG owner, ULONG procedure,
                             ULONG parameter);
BOOL WinXTranslateMsg(ULONG anchor, PVOID input, PVOID output);
void open32SyncCleanup(void);
void open32DeferredCleanup(void);
void open32DialogsCleanup(void);
BOOL open32CloseSync(ULONG handle);
int open32WaitSync(ULONG handle, ULONG timeout, ULONG *result);
void open32RegisterThreadQueue(void);
void open32UnregisterThreadQueue(void);
BOOL open32ThreadQueueWindow(ULONG window);
void open32ClipCursorPoint(LONG *x, LONG *y);
BOOL open32WindowUpdateLocked(ULONG window);
ULONG open32DragMessage(ULONG window, BOOL accept, ULONG message, ULONG first, ULONG second);
#endif
