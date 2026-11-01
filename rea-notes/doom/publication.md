# Doom integration with main

The Doom commit is based on main's `f1df1ef39b2e089482165b3e0555bb7eb9eeae98`
(Mesa OpenGL/PGL), following the merged Word Pro input/rendering commit
`496742a0d40f611621ca64514f6396fea0509c95`. Both sets of changes are preserved.
The newer PM navigation/control-key implementation supplies Doom's enhanced
scan codes without adding a duplicate translator. Cross-thread delivery,
DIVE/visible regions, sound and legacy sockets remain the Doom additions.

The publication is prepared in the isolated worktree
`.state/2ine/doom-publish/tree`. The original mixed working tree is left
unchanged. Workspace-only Doom notes and byte-identical executable launchers
are retained under this directory; the launchers install through the new-file
patch described in README.md. Game assets, user SoundFonts, ROMs and compiled
workers are not added to Git.

## Fullscreen integration correction

The combined run exposed two asynchronous startup/transition issues. SDL's
software renderer can redirect pending canvas commands after a window-surface
change. The HAL now flushes canvas drawing before showing or changing the
window's fullscreen state. SDL_SyncWindow serializes pending state before
capturing placement and before restoring position/size. These operations stay
on the video thread and preserve the existing graphics-release handoff.

This follows the pinned SDL3.4.18 implementation in
`src/render/software/SDL_render_sw.c` (SW_WindowEvent, SW_ActivateRenderer)
and `src/render/SDL_render.c` (SDL_SetRenderTarget), and the SDL_video.h
SDL_SyncWindow contract. The fullscreen test waits for its actual expected
pixels, uses a guest key/pointer acknowledgment before injecting the click,
and waits for click delivery. It still requires exact colors, coordinates,
restored pixels/placement, Return suppression and F11 delivery.
Ten consecutive complete fullscreen tests pass on each ABI, including both
startup and shortcut modes. Retained output:
`.state/2ine/doom-publish/fullscreen-stable-{native,portable}.log`.

Final tested file hashes:

- hal/sdl/video.c: `7475345a1e011493dc5e3482e1ff3b6c9742f01b334ea6fafbb071a14a3c562f`.
- tests/integration/fullscreen.py: `b5f99dcc487c1969039c8985e5b3209fd85e4c0c12b5f4f4e1b6b1d547981a65`.

## Verification

Both publication builds use the same retained pinned inputs as finish.md and
explicitly enable TWOINE_OPENGL. Native GCC i386 and portable Clang/Unicorn
builds succeed. Full configured suites pass31/31 native and36/36 portable,
including Mesa pixels, PGL/PM and actual X11 desktop context handoff. This
includes the current Word Pro PM contracts; no new physical-device or macOS
verification is inferred. Owning Python tools pass40 tests. Ruff lint/format,
ty, both generated-binding checks, full C/H formatting and diff checks pass.

Final real-game acceptance uses:

```
env PATH=/usr/bin:/bin python3 .agents/skills/os2-re-2ine/scripts/2ine_verify.py --spec .state/2ine/doom-publish/verify-main.json --marker .state/2ine/doom-publish/DONE-main --log .state/2ine/doom-publish/DONE-main.log
```

It runs both configured suites and both real Doom gameplay/network harnesses
against these exact builds. The separate application executable remains
`7604e9aa38e91c862ea07cd25ec4aa7578a912b844c75ad7d62cb9907b532aee`;
its two clean source builds are unchanged. The selected SC-55 bank, save-file
checks, fullscreen, stereo and worker identities remain explicit assertions.
The verifier passed and wrote DONE-main (exit0). Aggregate evidence is
`.state/2ine/doom-publish/main-result.json`; detailed receipts are
`main-gameplay-{native,portable}/result.json` and
`main-network-{native,portable}/result.json`. Both game runs lasted70.03 seconds,
verified combat and >81KB save/reload, retained distinct stereo channels and
exited normally. Both network pairs recorded identical command streams.
Worker/library identities stayed unchanged throughout each run.
