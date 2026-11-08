/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OPEN32_CLASSES_H
#define TWOINE_OPEN32_CLASSES_H
#include "os2libs/os2native.h"
#define OPEN32_BUILTIN_CONTROL 3

typedef struct Open32Class
{
    ULONG atom, instance, style, procedure, icon, small_icon, cursor, brush;
    ULONG class_extra, window_extra, menu_id;
    unsigned references;
    char *name, *menu;
    void *class_data;
    LxGuestMapping *names, *procedure_mapping;
    ULONG builtin; /* 1 MDI client, 2 scroll bar, OPEN32_BUILTIN_CONTROL PM control. */
    struct Open32Class *next, *alias;
} Open32Class;

/* An acquired class cannot be unregistered until its reference is released. */
Open32Class *open32ClassAcquire(ULONG instance, PSZ name);
void open32ClassRelease(Open32Class *class);
#endif
