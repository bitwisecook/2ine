/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Private window manager interfaces shared by 2ine's DLLs: windows, classes, queues and anchor
   blocks, and the menu control's items. */
#ifndef _INCL_PMWINP_H_
#define _INCL_PMWINP_H_
#define FIRST_HWND_VALUE 10
#define FIRST_HPS_VALUE 10
#define SET_WIN_ERROR_AND_RETURN(anchor, err, rc)                                                  \
    {                                                                                              \
        lxHostThreadState()->pm_error = (err);                                                     \
        return (rc);                                                                               \
    }
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmp.h"
#include "host/window.h"
#include "host/input.h"
#include "host/desktop.h"
#include "host/font.h"
#include "host/clock.h"
#include "host/process.h"
#include "host/sync.h"
#include "emulator/handles.h"

typedef struct WindowClass
{
    char *name;
    PFNWP window_proc;
    ULONG style;
    ULONG data_len;
    struct WindowClass *next;
} WindowClass;

typedef struct
{
    LxHostWindow *window;
} HeavyWeightWindow;

typedef struct PmPresentationParameter
{
    ULONG id, size;
    struct PmPresentationParameter *next;
    uint8 data[];
} PmPresentationParameter;

typedef struct Window
{
    PFNWP window_proc;
    HeavyWeightWindow heavy;
    struct Window *parent;
    struct Window *children;
    struct Window *sibling;
    struct Window *owner;
    const char *
        window_class_name; /* Retain the creation-time class for diagnostics; styles and procedures can vary. */
    char *text;
    PmPresentationParameter *parameters;
    struct PmDialog *dialog;
    struct PmControl *control;
    struct PmMenu *menu;
    struct PmFileDialog *standard_file;
    struct PmFontDialog *standard_font;
    BOOL destroying;
    HWND last_focus;
    LxHandleBorrow accelerator;
    void *data;
    size_t data_len;
    ULONG id;
    ULONG class_style;
    ULONG style;
    ULONG frame_flags;
    BOOL formatting_frame;
    BOOL flash;
    ULONG flash_phase, flash_count;
    BOOL frame_chrome_hidden;
    BOOL frame_chrome_visible;
    ULONG frame_tracking;
    POINTL frame_track_start;
    RECTL frame_track_rect;
    HWND hwnd;
    LONG x;
    LONG y;
    LONG w;
    LONG h;
    RECTL invalid;
    RECTL *damage_rects;
    size_t damage_count;
    BOOL paint_pending;
    BOOL paint_offered;
    uint64_t last_present;
    BOOL enabled;
    BOOL visible_region_notify;
} Window;

static inline LONG clamp_long(LONG x, LONG low, LONG high)
{
    return x < low ? low : x > high ? high : x;
}

LX_PRIVATE void cleanupErrorInfo(void);
/* FC_NOSETFOCUS, FC_NOLOSEFOCUS and FC_NOSETACTIVE suppress their notifications. */
LX_PRIVATE BOOL changeFocus(HWND desktop, HWND handle, ULONG flags);
LX_PRIVATE BOOL queryPointerClip(RECTL *bounds);
/* FOCUS_NOTIFY adds WM_FOCUSCHANGE and WM_SETSELECTION for WinFocusChange callers. */
#define FOCUS_NOTIFY 0x10000u
/* Decodes a 16-entry string or message bundle resource. */
LX_PRIVATE LONG loadStringTable(HMODULE module, ULONG type, ULONG id, LONG capacity, PCHAR output);
LX_PRIVATE BOOL prepareFrameState(Window *window, SWP *position);
LX_PRIVATE BOOL isFrameWindow(const Window *window);
LX_PRIVATE BOOL frameGuestChrome(const Window *window);
LX_PRIVATE BOOL frameResizable(const Window *window);
LX_PRIVATE BOOL frameControlSuppressed(const Window *window);
LX_PRIVATE void applyHostWindowChrome(Window *window);
LX_PRIVATE MRESULT frameSystemCommand(Window *window, USHORT command);
LX_PRIVATE BOOL frameTrackMessage(Window *window, ULONG message, MPARAM first);
LX_PRIVATE POINTL frameBorderSize(const Window *window);
LX_PRIVATE BOOL frameClientRect(Window *window, PRECTL rect, BOOL client);
LX_PRIVATE void formatFrame(Window *window);
LX_PRIVATE ULONG frameControlCount(Window *window);
LX_PRIVATE ULONG frameFormatPositions(Window *window, MPARAM positions, MPARAM client_rect);
LX_PRIVATE void paintFrameBorder(Window *window, HPS space);
LX_PRIVATE MRESULT OS2API frameControlProcedure(HWND handle, ULONG message, MPARAM first,
                                                MPARAM second);
LX_PRIVATE LONG windowPresentationColor(Window *window, BOOL foreground, LONG fallback);

typedef struct MessageQueueItem
{
    QMSG qmsg;
    struct MessageQueueItem *next;
} MessageQueueItem;

typedef struct
{
    HMQ hmq;
    ULONG codepage;
    BOOL cancel_shutdown;
    POINTL last_position;
    MessageQueueItem *head;
    MessageQueueItem *tail;
    MessageQueueItem *free_pool;
} MessageQueue;

#include "os2/include/private/pmgpip.h"

typedef struct PmTimer
{
    HWND hwnd;
    ULONG id, interval;
    void (*callback)(void);
    uint64_t due;
    struct PmTimer *next;
} PmTimer;

typedef struct
{
    struct PmWindowEnumeration *enumerations;
    PmDeviceContext *devices;
    struct PmBitmap *bitmaps;
    struct PmAccelerator *accelerators;
    struct PmRegion *regions;
    struct PmPointer *pointers;
    struct PmHook *hooks;
    struct PmFileDialog *pending_file;
    LxGuestMapping *control_bridge, *menu_bridge, *frame_control_bridge;
    HWND focus, active, capture, click_window;
    ULONG click_ticks, click_button;
    LONG click_x, click_y;
    ULONG press_ticks[3];
    LONG press_x[3], press_y[3];
    BOOL press_down[3], press_moving[3];
    ULONG modifiers, text_modifiers, text_scancode, text_repeat;
    uint8 key_down[256], key_toggled[256], physical_keys[256];
    uint8 physical_buttons[3];
    PmTimer *timers;
    HAB hab;
    Window object_window;
    WindowClass *registered_classes;
    MessageQueue message_queue;
    unsigned paint_depth;
    Window **windows;
    size_t windows_array_len;
    PresentationSpace *pres_spaces;
    size_t pres_spaces_array_len;
} AnchorBlock;

/* Delivers to every queue in this process; wait blocks until each foreign queue took it. */
LX_PRIVATE BOOL broadcastQueues(AnchorBlock *self, ULONG message, MPARAM first, MPARAM second,
                                BOOL wait);
/* Posts the semantic message whose SV_*KB value names this virtual key and shift state. */
LX_PRIVATE BOOL postSemanticKey(AnchorBlock *anchor, HWND window, ULONG key, ULONG shift,
                                ULONG ticks);
extern LX_PRIVATE uint32 GHABCounter;
extern LX_PRIVATE uint32 GHMQCounter;
/* desktop_window, getAnchorBlockNoHAB and getWindowFromHWND are also used by PMCTLS. */
extern Window desktop_window;
LX_PRIVATE void destroyPresentationParameters(Window *window);
LX_PRIVATE void destroyWindowEnumerations(AnchorBlock *anchor);
AnchorBlock *getAnchorBlockNoHAB(void);
LX_PRIVATE AnchorBlock *getAnchorBlock(const HAB hab);
Window *getWindowFromHWND(const AnchorBlock *anchor, const HWND hwnd);
LX_PRIVATE PresentationSpace *getPresentationSpaceFromHPS(const AnchorBlock *anchor, const HPS hps);
LX_PRIVATE const char *messageName(const ULONG msg);
LX_PRIVATE WindowClass *findRegisteredClass(AnchorBlock *anchor, const char *classname);
LX_PRIVATE ULONG currentSystemTicks(void);
LX_PRIVATE PFNWP defaultGuestWindowProc(void);
LX_PRIVATE PFNWP defaultGuestDialogProc(void);
LX_PRIVATE MRESULT sendMessage(Window *win, ULONG msg, MPARAM mp1, MPARAM mp2);
LX_PRIVATE BOOL destroyMessageQueue(AnchorBlock *anchor);
LX_PRIVATE void destroyWindow(AnchorBlock *anchor, Window *win);
LX_PRIVATE void destroyPresentationSpace(AnchorBlock *anchor, PresentationSpace *ps);
LX_PRIVATE void discardWindowMessages(AnchorBlock *anchor, HWND window);
LX_PRIVATE ERRORID postMessage(AnchorBlock *anchor, const QMSG *qmsg);
LX_PRIVATE BOOL postTimestampedMsg(AnchorBlock *anchor, HWND hwnd, ULONG msg, MPARAM mp1,
                                   MPARAM mp2, ULONG ticks);
LX_PRIVATE BOOL postExposedMessages(AnchorBlock *anchor, Window *win, const ULONG ticks);

/* The WM_PAINT calls running on this thread, innermost first; presentation
   defers the canvases they draw on. */
typedef struct PaintScope
{
    AnchorBlock *anchor;
    HWND window;
    struct PaintScope *previous;
} PaintScope;

extern LX_PRIVATE LX_HOST_THREAD_LOCAL PaintScope *ActivePaint;
LX_PRIVATE void presentWindow(AnchorBlock *anchor, Window *window);
LX_PRIVATE void presentWindows(AnchorBlock *anchor);
/* The PM thread draws DIVE blits and decoded video frames that other threads
   submitted for its windows (dive_targets.c, video_frames.c). */
void lx_pm_dive_pump(void *anchor);
void lx_pm_dive_forget(void *window);
void lx_pm_dive_cleanup(void *anchor);
void lx_pm_video_pump(void *anchor);
void lx_pm_video_forget(void *window);
void lx_pm_video_cleanup(void *anchor);
LX_PRIVATE extern BOOL lxPmHostFocusReport;
LX_PRIVATE HWND registerWindowMessages(AnchorBlock *anchor);
LX_PRIVATE void unregisterWindowMessages(HWND window);
LX_PRIVATE void bindWindowTarget(Window *window);
LX_PRIVATE HWND foreignWindowQuery(HWND window, LONG code);
LX_PRIVATE BOOL windowOwnerThread(HWND window, ULONG *thread);
LX_PRIVATE void registerMessageQueue(AnchorBlock *anchor);
LX_PRIVATE void retireAnchorMessages(AnchorBlock *anchor);
LX_PRIVATE BOOL postForeignMessage(HWND window, ULONG message, MPARAM first, MPARAM second);
LX_PRIVATE BOOL windowMessageTargetExists(HWND window);
LX_PRIVATE void drainForeignMessages(AnchorBlock *anchor);
LX_PRIVATE void notifyVisibleRegions(AnchorBlock *anchor, Window *window);
BOOL processHostEvent(AnchorBlock *anchor, const LxHostEvent *event);
LX_PRIVATE void pumpEvents(AnchorBlock *anchor);
LX_PRIVATE BOOL skipMessage(const PQMSG qmsg, HWND hwndFilter, ULONG msgFilterFirst,
                            ULONG msgFilterLast);
LX_PRIVATE void os2ColorIndexToRGB(const LONG os2color, uint8 *r, uint8 *g, uint8 *b);
LX_PRIVATE void calcHeavyweightRect(const Window *win, LxHostRect *rect);
LX_PRIVATE int pumpTimers(AnchorBlock *anchor);
LX_PRIVATE MRESULT sendCreateMessage(Window *window, const CREATESTRUCT *creation);
LX_PRIVATE MRESULT sendWindowPos(Window *win, ULONG msg, SWP *position);
LX_PRIVATE BOOL sendEraseBackground(Window *client, HPS space, const RECTL *rectangle);
LX_PRIVATE void destroyDialog(Window *window);
LX_PRIVATE BOOL initializeDialog(Window *window);
LX_PRIVATE BOOL initializeDialogProcedure(HWND window, HWND focus, PVOID parameters);
LX_PRIVATE void dialogWordChanged(Window *window, LONG index, USHORT value);
LX_PRIVATE void updateWindowCyclePolicy(Window *window);
LX_PRIVATE BOOL windowAcceptsInput(Window *window);
LX_PRIVATE BOOL windowAcceptsUpdates(const Window *window);
LX_PRIVATE void destroyControl(Window *window);
LX_PRIVATE MRESULT controlProcedure(HWND window, ULONG message, MPARAM first, MPARAM second);
LX_PRIVATE HWND createDialog(HWND parent, HWND owner, PFNWP procedure, const uint8 *data,
                             size_t size, PVOID parameters, HMODULE module);
LX_PRIVATE void synchronizeInputState(AnchorBlock *anchor, const QMSG *message);
BOOL processKeyboardEvent(AnchorBlock *anchor, const LxHostEvent *event);
BOOL releaseInputState(AnchorBlock *anchor, ULONG timestamp);
LX_PRIVATE HWND topLevelWindow(AnchorBlock *anchor, HWND handle);
LX_PRIVATE void releaseWindowAccelerator(Window *window);
LX_PRIVATE void destroyAccelerators(AnchorBlock *anchor);
LX_PRIVATE HPS acquirePresentationSpace(HWND window, HPS ps, PRECTL rect, BOOL paint);
LX_PRIVATE PFNWP callableWindowProcedure(AnchorBlock *anchor, PFNWP procedure);
LX_PRIVATE MRESULT OS2API menuProcedure(HWND window, ULONG message, MPARAM first, MPARAM second);
LX_PRIVATE void destroyMenu(Window *window);
/* PMCTLS keeps its standard-dialog state on PMMERGE windows. PMMERGE does not
   link PMCTLS, so PMCTLS registers a hook that releases that state when a
   window is destroyed. */
typedef void (*PmWindowDestroyHook)(Window *window);
BOOL lx_pm_add_window_destroy_hook(PmWindowDestroyHook hook);
LX_PRIVATE MRESULT sendOwnerItem(Window *owner, ULONG id, OWNERITEM *item);
LX_PRIVATE void validateWindowPaint(AnchorBlock *anchor, Window *window);
LX_PRIVATE void destroyDeviceContexts(AnchorBlock *anchor);
LX_PRIVATE void destroyPointers(AnchorBlock *anchor);
LX_PRIVATE void destroySharedPointers(void);
LX_PRIVATE LONG queryPointerLevel(void);
LX_PRIVATE void destroyHooks(AnchorBlock *anchor);
LX_PRIVATE BOOL filterInputMessage(AnchorBlock *anchor, QMSG *message, ULONG flags);

/* Menus */
/* Menu entries retain command IDs, attributes and owned submenu windows. */
void dismissMenusForClick(AnchorBlock *anchor, Window *target);

typedef struct PmMenuItem
{
    MENUITEM item;
    char *text;
    RECTL rect;
} PmMenuItem;

typedef struct PmMenu
{
    PmMenuItem *items;
    size_t count;
    int selected;
    HWND focus;
    HWND active_parent;
    unsigned codepage;
} PmMenu;

LX_PRIVATE BOOL createSystemMenu(Window *frame, ULONG flags);
LX_PRIVATE BOOL createMinMaxMenu(Window *frame, ULONG flags);
Window *lx_pm_menu_window(HWND handle);
BOOL lx_pm_menu_attach(HWND menu, HWND parent);
BOOL lx_pm_menu_owner(HWND menu, HWND owner);
BOOL lx_pm_menu_remove(HWND handle, size_t index, BOOL destroy_submenu);
#endif
