# Game timedemos

Use the complete `demo1` timedemo at a fixed resolution to compare runtime
changes. Run one game at a time, using the same executable, game data, sound
settings and renderer. A window resize changes the workload.

From the checkout root, with the locally prepared applications:

```sh
python3 tools/benchmark_games.py glquake \
  --build build-portable --program build-apps/glquake/glquake.exe \
  --data build-apps/glquake/id1/pak0.pak --work build-perf/quake-run1

python3 tools/benchmark_games.py doom \
  --build build-portable --program build-apps/doom/os2doom.exe \
  --data build-apps/doom/doom1.wad --config build-apps/doom/default.cfg \
  --work build-perf/doom-run1
```

The default resolution is 640×480; `--width` and `--height` select another size.
Each run requires a fresh work directory and retains `runtime.log` and
`results.json`. The JSON records input hashes, runtime-library hashes, resolution,
the exact command and the engine's completed timedemo result. Doom's configuration
is copied, with only its resolution overridden; the interactive configuration
stays in its application directory. GLQuake uses fresh default settings and
records its renderer. Both run with sound enabled by their normal settings.

GLQuake's result counts rendered timedemo frames. Doom reports game tics and
elapsed real tics; FPS is `35 * gametics / realtics`. Doom normally advances
gameplay at 35 tics per second, so its timedemo FPS measures available processing
capacity rather than changing the gameplay clock. Compare each game with itself.

The GLQuake frame bound allows the bundled shareware demo to finish and then
shuts down automatically. A run that times out or lacks a completed FPS result
fails; it must not be reported as a measured speedup. The default timeout is
480 seconds and can be changed with `--timeout`.
