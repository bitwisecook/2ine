# DOSCALLS final export batch

Unbuilt and untested. Nothing in this note has been compiled or run; it records
static evidence and source intent only.

## Authorities

- DOSCALL1: `extracted/install-media/os2image/disk_2/DOSCALL1.DLL`, SHA-256
  `c1e39d8220b38de30f3f83ac33044e116cbe2ed384edf64160e8b1b8dd8cda16`, analysed
  through `analysis/decompiled/DOSCALL1/retail`.
- Kernel: `extracted/install-media/os2image/debug/UNI/OS2KRNLB`, SHA-256
  `9d0d7c5c5d7c9f772a68f8b81b1b6e282e01dfdf650da5060039e8a4f2505282`, analysed
  through `analysis/decompiled/OS2KRNL/checked`. This is the checked kernel, so
  offsets are specific to it.
- Toolkit errors: `sdk/os2tk45-master/h/bseerr.h`.

## Original behaviour established

### #161/#162 DosFSRamSemRequest/Clear (object 1+0xc, 1+0xb1)

DOSFSRSEM offsets: cb 0, pid 2, tid 4, cUsage 6, client 8, sem 0xa. The owner word
pair is compared as one dword. LINFOSEG `rfProcStatus` bit 0 (exit list) makes
the requester's TID 0xffff.

Request: free (pid 0) takes ownership with cUsage 1; same pid and tid increments
cUsage; same pid while the caller is in its exit list takes over, sets tid 0xffff
and cUsage 1, and returns 105 ERROR_SEM_OWNER_DIED; otherwise it sets the low byte
of sem to 0xff and waits. Clear: only the owner may release, or the same pid when
the owner tid is 0xffff; otherwise 101 ERROR_EXCL_SEM_ALREADY_OWNED. At cUsage 0 it
zeroes pid, tid and client and keeps only sem bits 0x0f00.

### #290/#351 DosStopTimer/DosStartTimer (kernel task/timapi.c)

_VR32StartTimer/_VR32AsyncTimer require `_SEMFindType` bits 0x10 (event) and 0x01,
else 323 ERROR_TS_SEMHANDLE. No free handle gives 324 ERROR_TS_NOTIMER; an unknown
handle gives 326 ERROR_TS_HANDLE. Interval 0 becomes 1. Handles are small indices
from 0, written as a word (`*(USHORT *)phtimer`); a null output is skipped. Stop
masks the handle to 16 bits and looks it up only in the calling process. Stop
waits for an in-flight firing. The kernel holds its own open on the semaphore for
the timer's life. Post failures are ignored. The periodic timer re-arms after each
post. Bit 0x01 was taken to be DC_SEM_SHARED; the code that sets it was not traced.

### #336 DosQueryMutexSem (kernel sem/semmtx.c)

Bad handle 6. Unowned: count 0 is written and pid and tid are left unchanged.
Owned: pid, tid and count are written. Owner died: the same three are written and
105 is returned.

### #222 DosSetFSInfo (kernel filesys/fsinfo.asm, w_SetFSInfo and cook_label)

Drive 0 means current; beyond the last drive 15 ERROR_INVALID_DRIVE. Level other
than 2 gives 124. cb 0 gives 122. Label length byte above 11 gives 154
ERROR_LABEL_TOO_LONG; length >= cb gives 122. Characters are case-mapped and
`*`, `?`, `.` give 123 ERROR_INVALID_NAME. The class table at LOWDATA+0x7bc passes
every byte value in this build under mask 0x28, so no other character is refused.
Zero length is accepted and clears the label. The label is space-padded to 11.

### #237/#238 DosResumeThread/DosSuspendThread (kernel _TKResumeThread, _TKSuspendThread)

Suspension is one flag, not a count. Suspend is idempotent and may target the
caller. An unknown TID, including 0, gives 309. Resume of a thread that is not
suspended gives 90 ERROR_NOT_FROZEN.
