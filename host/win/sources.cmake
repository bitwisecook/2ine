# Public domain. No rights reserved. Wholly AI-authored.
# Partial Windows host adapter, by function. host/platform.cmake stops a
# Windows configuration: fs, net and fatal have no Windows adapter.
twoine_host_function(system system/memory.c system/clock.c system/system.c)
twoine_host_function(process process/self.c process/child.c process/library.c)
twoine_host_function(thread thread/thread.c thread/sync.c)
twoine_host_function(ipc ipc/shared_memory.c)
twoine_host_function(clipboard clipboard/native_formats.c)
twoine_host_function(print print/dialog.c print/spool.c)
twoine_host_requires(print LINK comdlg32 winspool comctl32)
