/* Public domain. No rights reserved. Wholly AI-authored. */
/* Installer-operation lifetime is separate from guest parent/wait semantics. */
#ifndef TWOINE_HOST_PROCESS_SUPERVISOR_H
#define TWOINE_HOST_PROCESS_SUPERVISOR_H
#define LX_HOST_OPERATION_LEASE "TWOINE_OPERATION_LEASE_FD"
#define LX_HOST_OPERATION_OWNER "TWOINE_OPERATION_OWNER_FD"

/* Dedicated supervisor process only. Arguments omit the executable itself.
   SIGTERM/SIGINT cancel the owned group; SIGUSR2 forces cancellation. The
   supervisor reports completion only after all inherited worker leases close. */
int lx_host_operation_supervise(const char *executable, char *const *arguments,
                                char *const *environment);
/* Called once at worker startup, before guest execution or worker spawning.
   A missing scope succeeds; malformed/missing inherited channels fail. */
int lx_host_operation_join(void);
/* POSIX spawn adapter: 1 valid channels, 0 unscoped, -1 malformed. */
int lx_host_operation_channels(char *const *environment, int *lease, int *owner);
#endif
