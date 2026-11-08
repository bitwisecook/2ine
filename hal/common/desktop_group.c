/* Public domain. No rights reserved. Wholly AI-authored. */
/* Visible-window ownership elects one regular app; demotion precedes lease release. */
#include "hal/common/desktop_group.h"
#include "hal/common/desktop_lease.h"
#include <stdlib.h>

typedef struct DesktopWindow
{
    void *window;
    struct DesktopWindow *next;
} DesktopWindow;

struct LxDesktopGroup
{
    LxDesktopLease *lease;
    DesktopWindow *windows;
    LxDesktopPolicy policy;
    void *context;
    int auxiliary, regular;
};

LxDesktopGroup *lx_desktop_group_open(const char *directory, const char *session, int auxiliary,
                                      LxDesktopPolicy policy, void *context)
{
    if (!policy)
        return NULL;
    LxDesktopGroup *group = calloc(1, sizeof *group);
    if (!group)
        return NULL;
    group->policy = policy;
    group->context = context;
    group->auxiliary = auxiliary;
    if (!auxiliary)
        group->lease = lx_desktop_lease_open(directory, session);
    if ((!auxiliary && !group->lease) || !policy(context, 0))
    {
        lx_desktop_lease_close(group->lease);
        free(group);
        return NULL;
    }
    return group;
}

int lx_desktop_group_poll(LxDesktopGroup *group)
{
    if (!group)
        return 0;
    if (group->auxiliary)
        return 1;
    if (!group->windows)
    {
        if (group->regular && !group->policy(group->context, 0))
            return 0;
        group->regular = 0;
        lx_desktop_lease_release(group->lease);
        return 1;
    }
    if (!group->regular)
    {
        int claimed = lx_desktop_lease_claim(group->lease);
        if (claimed < 0)
            return 0;
        if (claimed && !group->policy(group->context, 1))
        {
            lx_desktop_lease_release(group->lease);
            return 0;
        }
        group->regular = claimed;
    }
    return 1;
}

int lx_desktop_group_visible(LxDesktopGroup *group, void *window, int visible)
{
    if (!group || !window)
        return 0;
    DesktopWindow **link = &group->windows;
    while (*link && (*link)->window != window)
        link = &(*link)->next;
    if (visible && !*link)
    {
        *link = calloc(1, sizeof **link);
        if (!*link)
            return 0;
        (*link)->window = window;
    }
    else if (!visible && *link)
    {
        DesktopWindow *old = *link;
        *link = old->next;
        free(old);
    }
    return lx_desktop_group_poll(group);
}

int lx_desktop_group_regular(const LxDesktopGroup *group)
{
    return group && group->regular;
}

int lx_desktop_group_close(LxDesktopGroup *group)
{
    if (!group)
        return 1;
    if (group->regular && !group->policy(group->context, 0))
        return 0;
    while (group->windows)
    {
        DesktopWindow *window = group->windows;
        group->windows = window->next;
        free(window);
    }
    lx_desktop_lease_close(group->lease);
    free(group);
    return 1;
}
