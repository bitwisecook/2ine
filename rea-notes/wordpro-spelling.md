# Original Word Pro dictionary initialization

The reported Word Pro dialog says it cannot find the spelling dictionary in
`C:\LOTUSW4\COMPNENT\Spell`, with `(Division)` substituted as its language name.
The failure was reproduced in the actual original application's new-document
path and traced to missing OS/2 national-profile defaults. A generic NLS-backed
read-default correction now lets the original initializer select English and
open the installed dictionary. The latest bounded original-app acceptance is
recorded below; root still owns final native desktop verification.

## Artifact identity

Read-only source:
`build-lotus-visuals/wordpro-next-acceptance/WordPro.2ineball`.
The files below match the corresponding read-only originals in
`/Users/jimd/Documents/Lotus.2ineball` byte-for-byte. Hash inventory:
`build-lotus-visuals/spell-artifacts.json`.

| Artifact under LOTUSW4 | Bytes | SHA-256 |
| --- | ---: | --- |
| WORDPRO/WORDPRO.EXE | 4542816 | bf122341b274d7bee0909b66ebd6f5bc884d407828105e532761eb405e0e4387 |
| WORDPRO/wpeno70.dll | 418911 | 510c761259a7c78ba5cd4dfe4ed83cc0911a15886b72584cbdc17bb272193652 |
| COMPNENT/SPELL/LTSPLO04.DLL | 305070 | b17e0464bb987c5faf4e4131571e40b60f6134dc92bc56c3d9dccc6b4e47bb82 |
| COMPNENT/SPELL/lotusen2.dic | 311808 | 1b08e8fadb6b6b5734d24d25a77e83789716f5ad05ce6e5c1946f7cffc8b8769 |
| COMPNENT/SPELL/ltspho04.dll | 82551 | 07206e57d0228ed47a57fd47e1b1c731e053a7dff762fb7130bf5ab4250f2396 |
| COMPNENT/SPELL/ltspfo04.dll | 65142 | e257cc0f9b2ab239371573255ffbd9f0575c7c3328b957c768a8921207df13fc |
| COMPNENT/ltplr30.dll | 343992 | 196569d3f332190b2980f71fd03db911390103c7de3cc18f63362aae71b37dc1 |

Registry HKLM `Software\Lotus\Components\Spell\4.0`, `Data Path`, contains
`C:\LOTUSW4\COMPNENT\Spell`. HKCU's `Multi User Path` has the same directory
with a trailing backslash. The actual host directory is named `SPELL`.
`C:\OS2\l97user.ini`, `WordProUser/Language`, contains `EN`.

## Static evidence and ABI

Patched workspace REA/Ghidra 12.2 inspected the exact LTSPLO04 bytes:

- MainDictInit, analysis address 0x34ef0, export ordinal 22:
  `ev_ebcf91f6929e85f20a105ea63cc68bd6258b9d268c3ff5849375a68aafc7390c`.
- Language translation helper 0x35b7c:
  `ev_c6c20b5e0aee07a4bcaea8677b983c992e9c3fe6bc576cc12320622240e165ae`.
- ICBuffSize, address 0x329a0, ordinal 20:
  `ev_eb31e3c33b55c3d9b27efc58eee6a17345da6b1363ad415381198ef9d11a6cd3`.

ICBuffSize returns 0x650 (1616). Assembly at MainDictInit reads caller stack
arguments for the state buffer, directory and a 16-bit language identifier;
it returns with plain RET. The decompiler's two-argument prototype omits the
third argument and is not authoritative. Its language helper maps 0x0409 to
internal index0x10. The initializer constructs `lotus` + language suffix +
`2.dic`, tries the resulting file, and returns the internal index on success.
Its original imports are DOSCALLS and LTPLR30; LTPLR30 is installed in COMPNENT.
The actual calls below independently verify the recovered ABI and result.

WPENO70 is a resource-only LX DLL. A checked read of its original physical LX
resource pages/string bundles finds:

- 6010: `ltsplo04.dll`;
- 6012: `ltsuser1.udc`;
- 14356: ` (Division)`;
- 15562: the reported missing-spelling-dictionary format string.

Raw resource inventory: `build-lotus-visuals/spell-resource-strings.json`.
These IDs explain where the strings exist; they do not establish which language
or state Word Pro supplied at the error boundary. `(Division)` is not the name
of an absent installed dictionary. Its selection by the full application remains
unobserved in this probe.

## Headless original-code acceptance

Command from the repository root:

```
python3 build-lotus-visuals/probe-original-spelling.py
```

The script clones another private ball before any execution. It does not modify
the active acceptance ball, the source probe or the Documents ball. A generated
LX program loads the original LTSPLO04 by full guest path, resolves its actual
exports, checks ICBuffSize, and calls MainDictInit with the original state size,
configured directory and language 0x0409. Buffer canaries remain intact. Native
module lookup uses the immutable coherent acceptance runtime. SDL video/audio
are dummy; no GUI was opened and no desktop input occurred.

Result directory: `build-lotus-visuals/spell-probe-8dfsyjnu`.

| Directory argument | Process exit | ICBuffSize | MainDictInit | Actual file opened |
| --- | ---: | ---: | ---: | --- |
| C:\LOTUSW4\COMPNENT\Spell | 0 | 1616 | 16 | C:\LOTUSW4\COMPNENT\Spell\lotusen2.dic |
| C:\LOTUSW4\COMPNENT\SPELL | 0 | 1616 | 16 | C:\LOTUSW4\COMPNENT\SPELL\lotusen2.dic |

`result.json`, `mixed.stderr` and `upper.stderr` retain the actual observations.
Each trace is 5222 bytes. They show dictionary DosOpen, reads/seeks through its
real data and clean process teardown; no missing native dependency/import was
reported. This rules out a missing English dictionary, this mixed-case directory
spelling, the recovered initializer ABI and its basic file reads for this exact
headless call. It does not prove spelling suggestions/user dictionaries, other
languages or the full Word Pro initialization path.

## Earlier isolated-probe gate

The initial isolated initializer probe did not reproduce the application's error.
The subsequent reproduction below supersedes that earlier gate. No
application-specific workaround or substituted success was introduced.

## Actual failure and corrected national defaults

`build-lotus-visuals/probe-actual-spelling.py` clones the actual ModalAcceptance
ball into a new directory on every run. It compiles diagnostic copies of host
CPU dispatch, DOS module lookup and PM message pumping outside production source,
freezes and relocates their full native dependency closure, and runs with dummy
video/audio. One BM_CLICK invokes the original Welcome default action, matching
the root's Return reproduction; no desktop input, license action, active-ball
write or guest instruction/data patch is performed. Native digests, launch plan,
bounded18second stop and trace byte limits are retained with each result.

The failing original call is observed in `actual-spell-m6oj9ioj` and its expanded
caller evidence in `actual-spell-ke2l01as`, `actual-spell-4e0w4x4v` and
`actual-spell-cowpu5yz`, all under `build-lotus-visuals`. LTSPLO04 MainDictInit
receives the correct configured Spell directory and1616-byte state, but its
language argument's low16bits are0000. It returns0 before opening a dictionary;
the state remains uninitialized. After the original default document action,
WinMessageBox displays the exact missing-dictionary/`(Division)` error.

The caller is original WORDPRO.EXE, object0, guestbase0x10000. Its adapter at
0x1a64a copies the language word from the caller's stack and calls the actual
export. The caller at0x1a596 reads the word from dictionary-object+0x0e. Its
constructor at0x1a49d calls original helper0x30d9b and reads globalword0x6d6722
when the requested language is zero. Read-only CPU write observation proves the
global starts0; its lazy flag startsFF and is cleared by the original helper,
but the language is not populated in the failing run. This is not an absent
constructor, a missing dictionary or a mixed-case path failure.

Patched REA/Ghidra12.2 confirms helper0x30d9b's lazy flag and indirect settings
query in `spell-default-language-rea.json`, evidence
`ev_1c397234c63e1ea2b7570bf8a008eb867092f8d2daec12a8e1d5f1d5d5d052c2`.
Near that actual startup boundary Lotus queries PM_National/iCountry with
default-1, then attempts LANG fallback. The selected USERprofile contains no
PM_National records and SYSTEMprofile is empty.

A controlled new-clone comparison (`actual-spell-hsi1c9uo`) changes only its
private USERprofile's iCountry to1. Original Lotus code then writes0409 into
the global, passes0409 to MainDictInit and gets16, opening lotusen2.dic; the
exact warning is absent through the bounded run. This establishes the causal
input without replacing an original result or changing guest code.

[IBM PM Programming Reference Volume II, AppendixG](https://www.bitsavers.org/pdf/ibm/pc/os2/OS2_3.x/G25H-7191-00_OS2_WARP_V3_Presentation_Manager_Programming_Reference_Volume_2_Oct94.pdf)
defines PM_National/iCountry as a country integer (1 US English,44 UK English),
with the related date/time/currency keys. Toolkit4.5's actual
samples/os2/timeserv/udtime.c queries those user-profile values with
DosQueryCtryInfo fields as defaults. Current native NLS supports the US country
provider. PMShApi now exposes read-only PM_National defaults derived from that
provider's COUNTRYINFO for country, date/time, currency, precision and separators.
Stored USER/SYSTEM values take precedence through existing query semantics;
explicit zero/empty values remain authoritative. Private profile handles,
including handles to the same backing file, gain no virtual defaults. Reads,
size/data queries and enumeration do not materialize any INI entries.

The final original-app probe, **actual-spell-k4hl71bk**, has no diagnostic country
override and no stored PM_National records in either profile. With the production
fallback, original Lotus code writes0409, MainDictInit receives0409 and the real
Spell directory, returns16 and opens lotusen2.dic. The original warning is absent
through the same18second new-document reproduction. `spelling-verification.json`
records empty stored national entries and unchanged WORDPRO/LTSPLO04/dictionary
hashes matching the inventory above. No files in the active or Documents balls
were changed by this investigation.

The complete rebuilt runtime passes **66/66** in36.43seconds, including native
profile string/int/data/size checks, USER/SYSTEM precedence, explicit zero/empty
handling, private aliases and nonmaterialization, plus prior modal/frame/printing
regressions. Logs: `/tmp/2ine-national-profile-build.log` and
`/tmp/2ine-national-profile-tests.log`.

Remaining qualification: root's fresh native UI generation, spelling suggestions
and user dictionaries, other country providers and the original LANG fallback.
The bounded successful application path establishes this warning's fix; it does
not claim every Lotus spell feature or locale is implemented.
