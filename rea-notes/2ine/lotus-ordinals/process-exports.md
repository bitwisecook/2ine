# Process exports: PMSHAPI#119 and 13 PMWINX entries

Status: static RE and unbuilt source only. Nothing here has been compiled or run.

## Originals and method

- PMWINX: `extracted/install-media/os2image/FI/DAX/DLL/PMWINX.DLL`, SHA-256
  `07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`, object 1 base 0x10000.
- PMMERGE: `extracted/module-packages/8043b0806c2bf4be74f46840e7d38fde45470da3af602d8c3cf2bb10dee5cbe5/output.000.OS2_DLL_PMMERGE.DLL`,
  SHA-256 `f1f56353dde3c7bb4994ee0bfb903f9cb59cda1926993135f796c8cf433794c6`.
- PMSHAPI#119 is a forwarder (import module 1, ordinal 5385) to PMMERGE `WIN32STARTAPP`,
  object 4 offset 754864, a 32-to-16 thunk into `WINSTARTAPP` (#3281, object 2 offset 42832).

The retained decompilations in `evidence/root-decompile/PMWINX/decompiled/` are misleading on
their own. **PMWINX and PMMERGE use in-page fixup chains:** each fixup record names only the
chain head. Every site's dword holds the next page offset in its high 12 bits (0xfff ends the
chain) and the target offset in its low 20 bits. A value of zero at an import site means no
chain. `scripts/lx-fixups` lists heads only. That is why Ghidra shows `_DAT_fff007b0` style
addresses and loses import calls, which appear as `CONCAT31(x, n)` from the `MOV AL,n` Optlink
count. Findings below come from instruction listings joined with walked chains, not pseudocode.
All entries use stack arguments with a plain `RET`: LX_ABI_STACK32, callee_pop 0.

## Original behaviour (PMWINX)

- **CreateProcess #92** (0x97fcc). Takes the command up to the first space (no quote handling)
  as the name, limited to 0x103; arguments follow that space. `lpApplicationName` replaces the
  name but arguments still come from the command. A non-absolute name gets `.EXE` when it has
  no dot and must pass `DosSearchPath(7, "PATH", ...)`, whose result is discarded. Flag 0x200
  uses `DosStartSession` (independent, 60-byte STARTDATA) after `DosQueryAppType`; otherwise
  `DosExecPgm` with 8 -> EXEC_BACKGROUND, 4 -> EXEC_LOAD, 1 -> EXEC_ASYNCRESULTDB, else
  EXEC_ASYNC. App type bit 0x1000 redirects to `WINOS2.COM` with `/s name args` and an
  environment from WINOS2/PM_GlobalWindows31Settings. Both null names give error 2.
  `lpCurrentDirectory` ([EBP+0x24]) and `bInheritHandles` are **never read**. Afterwards it
  fills PROCESS_INFORMATION with thread id 1 and calls `DosSetPriority(0, class, 0, pid)`,
  ignoring the result: 0x20/0x80 -> 2, 0x40 -> 1, 0x100 -> 3, none inherits idle or regular.
  wShowWindow 0/2/3 map to PgmControl 1/4/2 under STARTF_USESHOWWINDOW; flags & 6 sets 0x8000.
- **Process/thread objects**: magic 0xface0004 / 0xface0006 in a shared table. Exit code at
  process+0x1a starts as 0x103. Thread suspend count at +0x46, DOS tid at +0x26.
- **DuplicateHandle #163** (0x97e2c). Access top byte nonzero -> 87. Unresolved process handle
  or failed duplicate -> 6. Options bit 1 closes the source after success.
- **ExitProcess #192**: records the code in the own process object, then `DosExit(1, code)`.
- **ExitWindows #194**: `PMWP#149(0, thread's hmq)`; both API arguments are ignored and PMWP's
  result is returned.
- **FatalAppExit #201**: if `WinQueryCapture(HWND_DESKTOP)`, `WinSetCapture(HWND_DESKTOP, 0)`;
  `WinMessageBox(1, 1, text, "FATAL EXIT", 0, 0x1040)`; tail-jumps `DosExit(1, uAction)`, so the
  exit code is the *action* argument.
- **FatalExit #202**: `DosExit(1, code)`.
- **OpenProcess #506**: pid 0 returns 0. Scans a 256-entry pid table; an unknown pid returns 0
  without setting an error.
- **SuspendThread #681**: count 0 -> set 1, leave the lock if suspending self, `DosSuspendThread`,
  reset on failure. Otherwise increments. Returns the previous count, or -1 with error 6.
- **ResumeThread #573** (context): count 1 -> `DosResumeThread`; decrements while positive.
- **TerminateThread #688**: `DosKillThread(tid)`; the exit code argument is unused.
- **WinExec #714**: zeroed STARTUPINFO with wShowWindow set but dwFlags 0, so show is unused;
  `CreateProcess(0, cmd, 0, 0, 0, 0x200, ...)`; closes both handles; returns 0x20. On failure
  returns GetLastError when it is 2, 3 or 11, else 0.
- **GetExitCodeProcess #1005**: null output or bad handle -> error 0x57, not 6.
- **ShellExecute #1024**: operation must be null or "open", file non-null, else 31. Full path is
  directory + `\` + file. An extension other than `.exe` goes to `WinQueryObject` then
  `WinOpenObject(obj, 0, 0)`, ignoring its result and returning -1; no object returns 2. No
  extension appends `.exe`. Programs use `WinStartApp(0, &details, params, 0, 0)` with
  fbVisible = (show == 0) and swpInitial.fl by show: 0 0x10, 1/9/10 0x1088, 2 0x488, 3 0x888,
  4/8 8, 5 0x88, 6/7 0x400, others 0. Success returns -1. PMERR_STARTED_IN_BACKGROUND also
  counts as success; PMERR_INVALID_APPL gives 11; others 31.
- **WaitForInputIdle #1028**: bad handle -> error 6, -1. Calls
  `PMMERGE#5474 Win32WaitForIdleThread(pid, 1, timeout)`: 0 -> 0, 640 -> 0x102, else -1.
  That function polls every 250 ms for the thread's queue, then waits on a queue event
  semaphore; a queue slot of -1 means it was already idle. This is a genuine signal.
- GetCurrentProcess/GetCurrentThread return **real** handles in the original; 2ine uses -1/-2.

## Original behaviour (WinStartApp, PMMERGE object 2)

fbOptions bits above 0x1f or non-null Reserved -> PMERR_INVALID_PARAMETERS 0x1208. Bad
hwndNotify -> PMERR_INVALID_WINDOW 0x1206. SAF_INSTALLEDCMDLINE takes pszParameters, blanked
when an unquoted `%` or `?` occurs; otherwise pszParams is used. SAF_BACKGROUND or a minimized
swp selects background; SAF_STARTCHILDAPP selects related. Program-type failure gives
PMERR_INVALID_APPL 0x1530. DosStartSession failure gives PMERR_DOS_ERROR 0x1200; error 457
returns the HAPP with warning 0x1532. The startup directory is applied with a temporary
process-global change inside DosEnterCritSec, which 2ine deliberately does not copy.
