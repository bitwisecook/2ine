/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Window classes: the preregistered WC_* classes and WinRegisterClass. */
#include "emulator/launch_record.h"
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmwctlp.h"
#include "os2/include/private/pmp.h"

WindowClass *findRegisteredClass(AnchorBlock *anchor, const char *classname)
{
    const uintptr_t last_predefined = PM_INTEGER_ATOM_BASE + PM_LAST_PREDEFINED_CLASS;
    uintptr_t intclass = (uintptr_t)classname;
    /* "#n" names the predefined class n. */
    if (classname && !(intclass >= WC_FRAME && intclass <= last_predefined) && classname[0] == '#')
    {
        char *end;
        unsigned long number = strtoul(classname + 1, &end, 10);
        if (*end == 0 && number >= 1 && number <= PM_LAST_PREDEFINED_CLASS)
            intclass = PM_INTEGER_ATOM_BASE + number;
    }
    if ((intclass >= WC_FRAME) && (intclass <= last_predefined))
    {

        switch (intclass)
        {

            case WC_FRAME:
            {
                /* Public WC_FRAME words extend through QWL_PSTATBLK. */
                static WindowClass frame = {"#1", WinDefWindowProc, CS_SIZEREDRAW,
                                            QWL_PSTATBLK + sizeof(ULONG), NULL};
                return &frame;
            }
            case WC_COMBOBOX:
            {
                static WindowClass combo = {"#2", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &combo;
            }
            case WC_BUTTON:
            {
                static WindowClass button = {"#3", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &button;
            }
            case WC_MENU:
            {
                static WindowClass menu = {"#4", menuProcedure, CS_SIZEREDRAW, 0, NULL};
                return &menu;
            }
            case WC_STATIC:
            {
                static WindowClass text = {"#5", controlProcedure, CS_SIZEREDRAW | CS_HITTEST, 4,
                                           NULL};
                return &text;
            }
            case WC_ENTRYFIELD:
            {
                static WindowClass entry = {"#6", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &entry;
            }
            case WC_LISTBOX:
            {
                static WindowClass list = {"#7", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &list;
            }
            case WC_SCROLLBAR:
            {
                static WindowClass scrollbar = {"#8", controlProcedure, CS_SIZEREDRAW, 0, NULL};
                return &scrollbar;
            }
            case WC_TITLEBAR:
            {
                static WindowClass title = {"#9", frameControlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &title;
            }
            case WC_APPSTAT:
            case WC_KBDSTAT:
            case WC_PECIC:
            case WC_DBE_KKPOPUP:
                return NULL;
            case WC_MLE:
            {
                static WindowClass mle = {"#10", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &mle;
            }
            case WC_SPINBUTTON:
            {
                static WindowClass spin = {"#32", controlProcedure, CS_SIZEREDRAW, 4, NULL};
                return &spin;
            }
            case WC_CONTAINER:
            case WC_SLIDER:
            case WC_VALUESET:
            case WC_NOTEBOOK:
            case WC_CIRCULARSLIDER:
            default:
                return NULL;
        }
    }
    else
    {
        for (WindowClass *i = anchor->registered_classes; i; i = i->next)
        {
            if (strcmp(i->name, classname) == 0)
            {
                return i;
            }
        }
    }
    return NULL;
}

BOOL WinRegisterClass(HAB hab, PSZ pszClassName, PFNWP pfnWndProc, ULONG flStyle,
                      ULONG cbWindowData)
{
    TRACE_NATIVE("WinRegisterClass(%u, '%s', %p, %u, %u)", (uint)hab, pszClassName, pfnWndProc,
                 (uint)flStyle, (uint)cbWindowData);

    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor)
    {
        return FALSE;
    }

    WindowClass *winclass = findRegisteredClass(anchor, pszClassName);
    if (!winclass)
    { // PM docs say this call can replace existing class.
        winclass = (WindowClass *)malloc(sizeof(WindowClass));
        if (!winclass)
        {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, FALSE);
        }
        else if ((winclass->name = strdup(pszClassName)) == NULL)
        {
            free(winclass);
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, FALSE);
        }
        winclass->next = anchor->registered_classes;
        anchor->registered_classes = winclass;
    }

    winclass->window_proc = pfnWndProc;
    winclass->style = flStyle;
    winclass->data_len = cbWindowData;

    return TRUE;
}
