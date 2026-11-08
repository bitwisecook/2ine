# Public domain. No rights reserved. Wholly AI-authored.
# POSIX host adapter shared by Linux and macOS, by function.
twoine_host_function(system system/memory.c system/clock.c)
twoine_host_function(process process/self.c process/child.c process/supervisor.c process/library.c)
twoine_host_function(thread thread/thread.c thread/sync.c)
twoine_host_function(ipc ipc/shared_memory.c ipc/mailbox.c)
twoine_host_function(fs fs/files.c)
twoine_host_function(net net/socket.c net/raw_socket.c net/interfaces.c)
twoine_host_function(fatal fatal/report.c)
file(STRINGS "${PROJECT_SOURCE_DIR}/VERSION" _twoine_version LIMIT_COUNT 1)
twoine_host_requires(fatal DEFINE TWOINE_VERSION_TEXT="${_twoine_version}")
twoine_host_function(desktop desktop/cycle_registry.c desktop/lease.c)
twoine_host_function(print print/cups_spool.c)
