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
        anchor->last_error = (err);                                                                \
        return (rc);                                                                               \
    }
#include "os2libs/os2native.h"
#include "os2libs/pmwin.h"
#include "hal/video.h"
#include "hal/font.h"
#include "hal/host.h"

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
    BOOL enabled;
} Window;

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

typedef struct
{
    HPS hps;
    Window *window;
    POINTL position;
    LONG color;
    int persistent;
    LxHostFont *font;
    unsigned font_index, font_pixels;
} PresentationSpace;

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
    PmTimer *timers;
    HAB hab;
    ERRORID last_error;
    WindowClass *registered_classes;
    MessageQueue message_queue;
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
LX_PRIVATE MRESULT sendMessage(Window *win, ULONG msg, MPARAM mp1, MPARAM mp2);
LX_PRIVATE BOOL destroyMessageQueue(AnchorBlock *anchor);
LX_PRIVATE void destroyWindow(AnchorBlock *anchor, Window *win);
LX_PRIVATE void destroyPresentationSpace(AnchorBlock *anchor, PresentationSpace *ps);
LX_PRIVATE void discardWindowMessages(AnchorBlock *anchor, HWND window);
LX_PRIVATE ERRORID postMessage(AnchorBlock *anchor, const QMSG *qmsg);
LX_PRIVATE BOOL postTimestampedMsg(AnchorBlock *anchor, HWND hwnd, ULONG msg, MPARAM mp1,
                                   MPARAM mp2, ULONG ticks);
LX_PRIVATE BOOL postExposedMessages(AnchorBlock *anchor, Window *win, const ULONG ticks);
LX_PRIVATE BOOL processHostEvent(AnchorBlock *anchor, const LxHostEvent *event);
LX_PRIVATE void pumpEvents(AnchorBlock *anchor);
LX_PRIVATE BOOL skipMessage(const PQMSG qmsg, HWND hwndFilter, ULONG msgFilterFirst,
                            ULONG msgFilterLast);
LX_PRIVATE void os2ColorIndexToRGB(const LONG os2color, uint8 *r, uint8 *g, uint8 *b);
LX_PRIVATE void calcHeavyweightRect(const Window *win, LxHostRect *rect);
LX_PRIVATE int pumpTimers(AnchorBlock *anchor);
LX_PRIVATE MRESULT sendCreateMessage(Window *window, const CREATESTRUCT *creation);
LX_PRIVATE MRESULT sendWindowPos(Window *win, ULONG msg, SWP *position);
#endif
