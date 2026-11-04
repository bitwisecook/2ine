/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_DESKTOP_LEASE_H
#define TWOINE_DESKTOP_LEASE_H
typedef struct LxDesktopLease LxDesktopLease;
LxDesktopLease *lx_desktop_lease_open(const char *directory, const char *session);
/* 1 owns the lease, 0 another process owns it, -1 a host error. */
int lx_desktop_lease_claim(LxDesktopLease *lease);
void lx_desktop_lease_release(LxDesktopLease *lease);
void lx_desktop_lease_close(LxDesktopLease *lease);
#endif
