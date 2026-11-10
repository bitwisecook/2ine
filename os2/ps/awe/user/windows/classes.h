/* Public domain. No rights reserved. Wholly AI-authored. */
/* Registered Open32 window classes and the built-in classes Open32 provides. */
#ifndef TWOINE_OPEN32_CLASSES_H
#define TWOINE_OPEN32_CLASSES_H
#include "os2/include/private/export.h"
/* Open32Class.builtin: which built-in class a class record provides, or 0. */
#define OPEN32_BUILTIN_MDI_CLIENT 1
#define OPEN32_BUILTIN_SCROLLBAR 2
#define OPEN32_BUILTIN_CONTROL 3 /* A Windows control class over a PM control */

typedef struct Open32Class
{
    ULONG atom, instance, style, procedure, icon, small_icon, cursor, brush;
    ULONG class_extra, window_extra, menu_id;
    unsigned references;
    char *name, *menu;
    void *class_data;
    LxGuestMapping *names, *procedure_mapping;
    ULONG builtin;
    struct Open32Class *next, *alias;
} Open32Class;

/* An acquired class cannot be unregistered until its reference is released. */
Open32Class *open32ClassAcquire(ULONG instance, PSZ name);
void open32ClassRelease(Open32Class *class);
#endif
