# Original REXX mixed-width selector contracts

Bounded task: resolve the first loader/execution failure observed while launching
SmartSuite's REXX `INSTALL.CMD` with IBM's original OS/2 interpreter DLLs. The
original media files remain private fixtures in `build-cmd-runtime-review/OS2/DLL`.
No original code is substituted by an alternate REXX interpreter.

## Observed evidence

Original `REXXAPI.DLL` from the OS/2 installation PACK archive has SHA-256
`1ce3584fefb2f4c2663946777e66507a8955fc084e8bc8cf7d4603b8ae1b9cfb`.
`lx_dump` identifies object 2 as USE16 executable code, size 2447 bytes, page 9.
The workspace's independent EXEPACK decoder was used to inspect that original
page without changing it. `ndisasm -b16` independently decodes its entry bytes:

```
0013  B8 0B 15       mov ax,0x150b
0016  8E E0          mov fs,ax
...
0038  68 00 00       push word <DOSCALLS ordinal 295>
003B  66 50          push eax
003D  67 66 0F B2 24 24   lss esp,[esp]
```

The first runtime failure was #GP at `ff8f:0016`, EAX=`150b`. Thus the first
fault is the fixed OS/2 GDT TIB selector load, rather than a missing callable
function. Object 2 has nineteen 16-bit-offset fixups importing DOSCALLS 295;
the first targets offset 57 (`0039`), the immediate in the `push` above. This
value is the flat ring-three stack/data selector. The import catalogue names it
`Thk32R3DS`. Treating it as a callable missing-import trampoline supplies an
unrelated address low word and would cause a later selector fault.

The existing native backend already translates OS/2 flat-data selector `53`
loads because Linux cannot install arbitrary OS/2 GDT entries. That same
instruction-emulation boundary now recognizes MOV FS of the original TIB selector
`150b` and substitutes Linux's per-thread reserved TIB TLS descriptor.

## Changes and validation

The portable backend publishes GDT `150b` against the process-wide TIB alias.
That alias is rebound when the serialized CPU switches guest threads, so a
saved FS descriptor cache remains valid across thread switches. Descriptor base
and permissions follow the existing shared TIB/TLS window; original DLL bytes
remain unchanged.

A new backend-neutral `lx_execution_flat_data_selector` supplies the current
flat selector. DOSCALLS 295 is a strict-resolver absolute export, tagged
`LX_EXPORT_ABSOLUTE`, instead of a pointer to code. LX ordinal/name fixups bypass
host-to-guest entry translation for such exports. Existing direct/indirect
exports retain tags zero/one, and indirect dereferences explicitly require one.
The strict resolver integration is owned by the parent task.

Passed on Unicorn ARM64 macOS, through unsandboxed execution calls:

```
cmake --build build-image-app-runtime --target doscalls lx_loader twoine_execution_tests -j 8
python3 tests/integration/exceptions.py --build build-image-app-runtime --backend unicorn
build-image-app-runtime/tests/twoine_execution_tests unicorn
```

The synthetic exception guest now loads FS=`150b`, checks the actual TIB chain,
and imports ordinal 295 into a 16-bit immediate, verifying that it equals the
active flat DS. The process execution regression additionally reads per-thread
TIB storage through fixed FS=`150b` before and after nested callbacks, including
the two-thread blocking rendezvous. All existing execution contracts pass.

A subsequent original-interpreter retry encountered a stale owner module after
the parent extended `GLoaderState`: the old `libquecalls.so` called a shifted
function pointer and crashed in `freeSegment` during bridge initialization. This
is a dependency rebuild requirement, not evidence about the updated REXX guest
path. Rebuild all native modules before judging the next original DLL run.

Native i386 execution remains locally unverified because the configured Colima
Docker socket is absent. The native MOV-FS translation and absolute selector
regression are included in the same tests for native CI. POP FS of a literal
OS/2 selector is not separately emulated; the observed IBM entry uses MOV FS,
and ordinary saved FS values are native selectors after translation.

## Subsequent original execution

The original REXX path exposed two more LX relocation contracts. Internal
16-bit selector fixups to a BIG data object must produce the flat data selector,
rather than the low word of the object's relocated base. Non-alias 16:32
internal pointers likewise select flat code or data according to the target
object. The synthetic `SELECTORS` guest in `exceptions.py` checks both classes
and pointer widths.

Original REXX then reached its system-service and shared-pool startup; those
results are recorded in [the service investigation](rexx-service-compatibility.md).
The later SmartSuite callback reached a real 32-to-16-bit `LSS` and exposed an
`OBJ_TILE` allocation prerequisite: original REXX requested flag `0x52`, but
2ine placed its stack above the 512-MiB tiled range. The failing stack address
was `2027BC58`, whose original thunk conversion truncated its derived selector
to `013F`. The parent task corrected the tiled allocation and canonical-selector behavior.
The extended original fixture subsequently passed its CMD callback, and the
actual SmartSuite root launcher reached INSTALL.EXE. Those results are separate
from the fixed GDT/absolute-import and selector-fixup checks above.
