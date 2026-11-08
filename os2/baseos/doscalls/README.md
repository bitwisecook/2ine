<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# DOSCALLS.DLL

Base OS API: files, memory, threads, processes, semaphores, modules,
exceptions, time, and the queue, session, national language, message, Vio
and Kbd entries that QUECALLS, SESMGR, NLS, MSG, VIOCALLS and KBDCALLS
forward to.

- IBM source: `E:\w45baseos\…\baseos\os2krnl\{task,memory,filesys,sem,loader,dev,perf}\`
  for the kernel API workers; DOSCALL1's own directory is unknown (inferred
  `w45baseos\…\baseos\doscall1`).
- Evidence: Kernel MAPs (`debug/{UNI,SMP}/os2krnl*.map`, catalogued in the
  workspace's `rea-notes/inventory/source-catalogue/kernel/`); the DOSCALL1
  MAP (`debug/UNI/doscall1.map`) has segments and public symbols only, no
  source files.
- In Warp 4.52: DOSCALLS is the kernel's built-in module; ordinals it does not
  implement resolve into DOSCALL1.DLL. The six forwarder DLLs name DOSCALLS
  ordinals that DOSCALL1 implements.
- In 2ine: one library provides DOSCALLS, covering both the kernel API
  workers and DOSCALL1. The forwarder DLLs name the same DOSCALLS ordinals
  as Warp's.
- Exports: `doscalls.def`.

## Units

A unit named after an IBM unit is placed where that unit implements the same
API area. The name does not claim that the code matches the IBM unit's
contents. Kernel units are under `os2krnl\`; "DOSCALL1" means the API's
public symbol is in DOSCALL1's MAP and no kernel unit implements it, so the
file has a descriptive name.

| 2ine unit | IBM unit | APIs |
|---|---|---|
| `filesys/file.c` | `filesys\file.asm`, `filesys\dosdel.c` | Open/OpenL, Delete, ForceDelete, Copy, Move, file mode |
| `filesys/fhandle.c` | `filesys\fhandle.asm` | Read, Write, SetFilePtr, Close, DupHandle, SetFileSize, handle state and type, Set(Rel)MaxFH, ResetBuffer |
| `filesys/lock.c` | `filesys\fs32api.asm`, `filesys\lock.asm` | SetFileLocks |
| `filesys/devioctl.c` | `filesys\devioctl.asm` | DevIOCtl, Dos16DevIOCtl(2) |
| `filesys/pipe.c` | `filesys\pipe.asm` | Dos16MakePipe |
| `filesys/nampipe.c` | `filesys\nampipe1.asm`, `nampipe2.asm` | WaitNPipe |
| `filesys/shutdown.c` | `filesys\shutdown.asm` | Shutdown |
| `filesys/finfo.c` | `filesys\finfo.asm` | Query/SetFileInfo, Query/SetPathInfo |
| `filesys/extattr.c` | `filesys\extattr.asm` | Extended attributes |
| `filesys/dossrch.c` | `filesys\dossrch.asm` | FindFirst/Next/Close |
| `filesys/path.c` | `filesys\path.asm` | Current directory, Create/DeleteDir |
| `filesys/metachar.c` | `filesys\metachar.asm` | EditName |
| `filesys/fsinfo.c` | `filesys\fsinfo.asm` | Query/SetFSInfo, volume labels |
| `filesys/fsctl.c` | `filesys\fsctl.asm` | QueryFSAttach |
| `filesys/getset2.c` | `filesys\getset2.asm` | Current disk, verify, DevConfig, version, machine mode, info segments |
| `filesys/codepg.c` | `filesys\codepg.asm` | QueryCp, SetProcessCp |
| `memory/vmapi.c` | `memory\vmapi.c` | Alloc/Set/Query/FreeMem, shared memory |
| `memory/selwrk.c` | `memory\selwrk.asm`, `memory\vmname.asm` | 16-bit segments, huge memory, CS aliases, flat/selector conversion |
| `sem/semevent.c` | `sem\semevent.c` | Event semaphores |
| `sem/semmtx.c` | `sem\semmtx.c` | Mutex semaphores, ownership and sharing |
| `sem/semmux.c` | `sem\semmux.c` | MuxWait semaphores |
| `sem/semtools.c` | `sem\semtools.c` | Semaphore names |
| `sem/sem.c` | `sem\sem.asm` | 16-bit RAM and file-system RAM semaphores |
| `task/tkthrd.c` | `task\tkthrd.c` | Create/Kill/WaitThread, NE threads |
| `task/tkforce.c` | `task\tkforce.c` | Suspend/ResumeThread |
| `task/schedapi.c` | `task\schedapi.asm` | Critical sections, priority |
| `task/tksleep.c` | `task\tksleep.c` | Sleep |
| `task/tkexec.c` | `task\tkexec.c`, `task\exec.asm` | ExecPgm, child launch |
| `task/task.c` | `task\task.asm` | KillProcess, WaitChild, CWait |
| `task/tkextlst.c` | `task\tkextlst.c` | ExitList, with Exit |
| `task/tkv1api.c` | `task\tkv1api.asm` | ScanEnv, GetEnv, GetPID |
| `task/except.c` | `task\except.c` | Exceptions and signals |
| `task/timapi.c` | `task\timapi.c` | Async/Start/StopTimer |
| `task/timserv.c` | `task\timserv.asm`, `task\time.asm` | Get/SetDateTime |
| `task/tkinit.c` | `task\tkinit.c` | QuerySysInfo |
| `task/procstat.c` | `task\procstat.asm` | Dos16QProcStatus |
| `perf/qstatec.c` | `perf\qstatec.c` | QuerySysState |
| `loader/ldrrun.c` | `loader\ldrrun.asm` | LoadModule, module handles and names, QueryAppType |
| `loader/ldrsubr.c` | `loader\ldrsubr.c` | QueryProcAddr, FreeModule, QueryHeaderInfo, QueryProcType |
| `loader/ldrrsrc.c` | `loader\ldrrsrc.asm` | Get/FreeResource, QueryResourceSize |
| `dev/beep.c` | `dev\beep.asm` | Beep |
| `dev/tmr.c` | `dev\tmr.asm` | TmrQueryFreq/Time |
| `doscall1/searchpath.c` | DOSCALL1 (`DOS32SEARCHPATH`) | SearchPath |
| `doscall1/libpath.c` | DOSCALL1 (`DOS32QUERYEXTLIBPATH`) | Query/SetExtLIBPATH |
| `doscall1/info_blocks.c` | DOSCALL1 (`DOS32GETINFOBLOCKS`) | GetInfoBlocks |
| `doscall1/must_complete.c` | DOSCALL1 (`DOS32ENTERMUSTCOMPLETE`) | Enter/ExitMustComplete |
| `doscall1/thread_memory.c` | DOSCALL1 (`DOS32ALLOCTHREADLOCALMEMORY`) | Alloc/FreeThreadLocalMemory |
| `doscall1/suballoc.c` | DOSCALL1 (`DOS32SUBALLOCMEM`) | 32-bit suballocation |
| `doscall1/suballoc16.c` | DOSCALL1 (`DOSSUBALLOC`) | 16-bit suballocation |
| `doscall1/thunk.c` | DOSCALL1 (`THK32_UNITHUNK`, ordinal 547) | Universal thunk |
| `doscall1/queue.c` | DOSCALL1 (`DOS32READQUEUE`, …) | Queues (QUECALLS) |
| `doscall1/session.c` | DOSCALL1 (`DOS32STARTSESSION`, `DOSSMSETTITLE`, …) | Sessions (SESMGR) |
| `doscall1/nls.c` | DOSCALL1 (`DOS32QUERYCTRYINFO`, …) | National language support (NLS) |
| `doscall1/msg.c` | DOSCALL1 (`DOS32TRUEGETMESSAGE`, …) | Message files (MSG) |
| `doscall1/vio.c` | DOSCALL1 (`VIOGETMODE`, …) | Vio (VIOCALLS) |
| `doscall1/kbd.c` | DOSCALL1 (`KBDCHARIN`, …) | Kbd (KBDCALLS) |
| `guarded.c` | none | Handle-borrow and lock helpers shared by the units |
| `module.c` | none | DLL entry, export table, DosFlatToSel/DosSelToFlat entries |
