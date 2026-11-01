/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#ifndef TWOINE_OS2LIBS_PMWIN_INTERNAL_H
#define TWOINE_OS2LIBS_PMWIN_INTERNAL_H
#define FIRST_HWND_VALUE 10
#define FIRST_HPS_VALUE 10
#define SET_WIN_ERROR_AND_RETURN(anchor, err, rc)                                                  \
    {                                                                                              \
        lxHostThreadState()->pm_error = (err);                                                     \
        return (rc);                                                                               \
    }
#include "os2libs/os2native.h"
#include "os2libs/pmwin.h"
#include "hal/video.h"
#include "hal/font.h"
#include "hal/host.h"
#include "guest/handles.h"

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
    BOOL destroying;
    HWND last_focus;
    LxHandleBorrow accelerator;
    void *data;
    size_t data_len;
    ULONG id;
    ULONG class_style;
    ULONG style;
    HWND hwnd;
    LONG x;
    LONG y;
    LONG w;
    LONG h;
    RECTL invalid;
    uint64_t last_present;
    BOOL enabled;
    BOOL visible_region_notify;
} Window;
LX_PRIVATE BOOL prepareFrameState(Window *window, SWP *position);

typedef struct MessageQueueItem
{
    QMSG qmsg;
    struct MessageQueueItem *next;
} MessageQueueItem;

typedef struct
{
    HMQ hmq;
    MessageQueueItem *head;
    MessageQueueItem *tail;
    MessageQueueItem *free_pool;
} MessageQueue;

typedef struct PresentationSpace
{
    HPS hps;
    Window *window;
    POINTL position;
    LONG color;
    LONG back_color, mix, back_mix, line_type;
    LONG pattern_set, pattern;
    POINTL pattern_reference;
    LONG primitive_color[5], primitive_back[5];
    LONG primitive_mix[5], primitive_back_mix[5];
    RECTL viewport;
    BOOL viewport_set;
    SIZEL page_size;
    struct PmSavedSpace *saved;
    LONG save_id;
    LxHandleBorrow bitmap_lease, clip_lease;
    int persistent;
    LxHostFont *font;
    unsigned font_index, font_pixels;
    struct
    {
        unsigned index, pixels;
        BOOL defined;
    } logical_fonts[255];
    LxHandleBorrow bitmap_ids[255];
    char font_names[255][8];
    LONG charset, char_mode;
    LONG char_width, char_height;
    HDC dc;
    LxHandleBorrow dc_lease;
    ULONG codepage;
    BOOL rgb_colors;
    LONG color_format;
    LONG colors[256];
    uint8 color_valid[256];
} PresentationSpace;
typedef struct PmSavedSpace
{
    struct PmSavedSpace *next;
    LONG id;
    POINTL position;
    LONG color, back_color, mix, back_mix, line_type;
    LONG pattern_set, pattern;
    POINTL pattern_reference;
    LONG primitive_color[5], primitive_back[5];
    LONG primitive_mix[5], primitive_back_mix[5];
    LONG charset, char_mode, char_width, char_height;
    RECTL viewport;
    BOOL viewport_set;
    SIZEL page_size;
    BOOL rgb_colors;
    LONG color_format;
    LONG colors[256];
    uint8 color_valid[256];
} PmSavedSpace;
typedef struct PmDeviceContext
{
    void *anchor;
    HDC handle;
    LONG type;
    unsigned spaces;
    struct PmDeviceContext *next;
} PmDeviceContext;

typedef struct PmTimer
{
    HWND hwnd;
    ULONG id, interval;
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
    LxGuestMapping *control_bridge, *menu_bridge;
    HWND focus, active, capture;
    ULONG modifiers, text_modifiers, text_scancode, text_repeat;
    uint8 key_down[256], key_toggled[256], physical_keys[256];
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
extern LX_PRIVATE uint32 GHABCounter;
extern LX_PRIVATE uint32 GHMQCounter;
extern LX_PRIVATE Window desktop_window;
LX_PRIVATE void destroyPresentationParameters(Window *window);
LX_PRIVATE void destroyWindowEnumerations(AnchorBlock *anchor);
LX_PRIVATE AnchorBlock *getAnchorBlockNoHAB(void);
LX_PRIVATE AnchorBlock *getAnchorBlock(const HAB hab);
LX_PRIVATE Window *getWindowFromHWND(const AnchorBlock *anchor, const HWND hwnd);
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
LX_PRIVATE void presentWindow(AnchorBlock *anchor, Window *window);
LX_PRIVATE HWND registerWindowMessages(AnchorBlock *anchor);
LX_PRIVATE void unregisterWindowMessages(HWND window);
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
LX_PRIVATE void destroyDialog(Window *window);
LX_PRIVATE BOOL initializeDialog(Window *window);
LX_PRIVATE BOOL initializeDialogProcedure(HWND window, HWND focus, PVOID parameters);
LX_PRIVATE void dialogWordChanged(Window *window, LONG index, USHORT value);
LX_PRIVATE BOOL windowAcceptsInput(Window *window);
LX_PRIVATE BOOL windowAcceptsUpdates(const Window *window);
LX_PRIVATE void destroyControl(Window *window);
LX_PRIVATE MRESULT controlProcedure(HWND window, ULONG message, MPARAM first, MPARAM second);
LX_PRIVATE HWND createDialog(HWND parent, HWND owner, PFNWP procedure, const uint8 *data,
                             size_t size, PVOID parameters);
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
LX_PRIVATE void destroyStandardFile(Window *window);
LX_PRIVATE MRESULT sendOwnerItem(Window *owner, ULONG id, OWNERITEM *item);
LX_PRIVATE void validateWindowPaint(AnchorBlock *anchor, Window *window);
LX_PRIVATE void destroyDeviceContexts(AnchorBlock *anchor);
LX_PRIVATE void destroyPointers(AnchorBlock *anchor);
LX_PRIVATE void destroyHooks(AnchorBlock *anchor);
LX_PRIVATE BOOL filterInputMessage(AnchorBlock *anchor, QMSG *message, ULONG flags);
#endif
