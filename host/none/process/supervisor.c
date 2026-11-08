/* Public domain. No rights reserved. Wholly AI-authored. */
/* Installer-operation supervision for hosts without process-group leases.
   Workers run unscoped, and a supervisor request fails. */
#include "host/process_supervisor.h"
#include <stdio.h>

int lx_host_operation_supervise(const char *executable, char *const *arguments,
                                char *const *environment)
{
    (void)executable;
    (void)arguments;
    (void)environment;
    fprintf(stderr, "Installer operation supervision is unavailable on this host\n");
    return 1;
}

int lx_host_operation_join(void)
{
    return 1;
}

int lx_host_operation_channels(char *const *environment, int *lease, int *owner)
{
    (void)environment;
    if (lease)
        *lease = -1;
    if (owner)
        *owner = -1;
    return 0;
}
