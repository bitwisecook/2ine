/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_DESKTOP_GROUP_H
#define TWOINE_DESKTOP_GROUP_H
typedef struct LxDesktopGroup LxDesktopGroup;
typedef int (*LxDesktopPolicy)(void *context, int regular);
LxDesktopGroup *lx_desktop_group_open(const char *directory, const char *session, int auxiliary,
                                      LxDesktopPolicy policy, void *context);
int lx_desktop_group_visible(LxDesktopGroup *group, void *window, int visible);
int lx_desktop_group_poll(LxDesktopGroup *group);
int lx_desktop_group_regular(const LxDesktopGroup *group);
int lx_desktop_group_close(LxDesktopGroup *group);
#endif
