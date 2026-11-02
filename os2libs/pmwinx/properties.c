/* Public domain. No rights reserved. Wholly AI-authored. */
/* Per-window properties retain atom keys and opaque application data. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
typedef struct Open32Property
{
    ULONG atom, data;
    BOOL owns_atom;
    struct Open32Property *next;
} Open32Property;
static ULONG property_atom(PSZ name, BOOL add)
{
    if ((uintptr_t)name <= UINT16_MAX)
        return (ULONG)(uintptr_t)name;
    return add ? GlobalAddAtom(name) : GlobalFindAtom(name);
}
BOOL SetProp(ULONG handle, PSZ name, ULONG data)
{
    Open32Window *window = open32PropertyWindow(handle);
    if (!window)
    {
        open32SetError(1400);
        return FALSE;
    }
    ULONG atom = property_atom(name, TRUE);
    if (!atom)
    {
        open32SetError(87);
        return FALSE;
    }
    BOOL owns = (uintptr_t)name > UINT16_MAX;
    for (Open32Property *property = window->properties; property; property = property->next)
        if (property->atom == atom)
        {
            property->data = data;
            if (owns && property->owns_atom)
                GlobalDeleteAtom(atom);
            else if (owns)
                property->owns_atom = TRUE;
            return TRUE;
        }
    Open32Property *property = calloc(1, sizeof *property);
    if (!property)
    {
        if (owns)
            GlobalDeleteAtom(atom);
        open32SetError(8);
        return FALSE;
    }
    property->atom = atom;
    property->data = data;
    property->owns_atom = owns;
    property->next = window->properties;
    window->properties = property;
    return TRUE;
}
ULONG GetProp(ULONG handle, PSZ name)
{
    Open32Window *window = open32PropertyWindow(handle);
    if (!window)
    {
        open32SetError(1400);
        return 0;
    }
    ULONG atom = property_atom(name, FALSE);
    for (Open32Property *property = window->properties; property; property = property->next)
        if (property->atom == atom)
            return property->data;
    return 0;
}
ULONG RemoveProp(ULONG handle, PSZ name)
{
    Open32Window *window = open32PropertyWindow(handle);
    if (!window)
    {
        open32SetError(1400);
        return 0;
    }
    ULONG atom = property_atom(name, FALSE);
    Open32Property **link = &window->properties;
    while (*link && (*link)->atom != atom)
        link = &(*link)->next;
    if (!*link)
        return 0;
    Open32Property *property = *link;
    ULONG data = property->data;
    *link = property->next;
    if (property->owns_atom)
        GlobalDeleteAtom(property->atom);
    free(property);
    return data;
}
void open32PropertiesCleanup(Open32Window *window)
{
    while (window->properties)
    {
        Open32Property *property = window->properties;
        window->properties = property->next;
        if (property->owns_atom)
            GlobalDeleteAtom(property->atom);
        free(property);
    }
}
