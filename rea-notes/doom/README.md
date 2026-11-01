# Doom workspace evidence

These notes retain the Doom investigation and final verification from the OS/2
workspace. Paths in the records are relative to that workspace, which has no
Git repository. Runtime/game/ROM/SoundFont assets remain local.

The standalone 2ine build and Doom commands are documented in
[the Doom guide](../../docs/doom.md). The workspace launchers are retained as
`workspace-launchers.patch`. To install them into a fresh OS/2 workspace:

```sh
cd /path/to/os2
git apply /path/to/2ine/rea-notes/doom/workspace-launchers.patch
```

The workspace expects this checkout at `tools/src/2ine-hal`; existing scripts
are left in place because applying a new-file patch fails if they exist. Game
files, verified runtime directories and the audio preference are separately
prepared local inputs, as described in the guide and evidence.

[Integration with main](publication.md) records the merged Word Pro/OpenGL base,
fullscreen startup correction and final combined verification.
