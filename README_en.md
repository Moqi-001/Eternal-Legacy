# Eternal Legacy — PS Vita port

[中文](README.md) | [English](README_en.md)

Runs the Android version of *Eternal Legacy* on the PlayStation Vita: so_loader loads the
original `libEternalLegacy.so` directly, the Android JNI / GLES / audio interfaces are
replaced with Vita implementations, and rendering goes through vitaGL.

## Requirements

| Item | Notes |
|---|---|
| Hardware | PS Vita / PSTV |
| Firmware | 3.60 or 3.65 + HENkaku/Enso |
| Plugins | [kubridge.skprx](https://github.com/bythos14/kubridge) (RWX memory), [libshacccg.suprx](https://github.com/Rinnegatamante/ShaRKBR33D) (for vitaGL) |
| Game data | Extract from your own copy of the Android APK. **No game data is included in this repository.** |

## Data layout

Default is `ux0:data/EternalLegacy/`, changeable with `-DDATA_PATH=`:

```
ux0:data/EternalLegacy/
├── lib/armeabi-v7a/
│   └── libEternalLegacy.so      the one from lib/armeabi-v7a/ in the APK
├── gameloft/games/GloftELHP/    the APK's sdcard data (file*.dat, fonts, LOGO_*.mp4, ...)
└── files/                       save files, created on first run
```

The engine hardcodes its resource path as `/sdcard/gameloft/games/GloftELHP/` and its
writable path as `/data/data/com.gameloft.android.TBFV.GloftELHP.ML/`; both are remapped
to the directories above at startup.

## Install and first run

1. Install `EternalLegacy.vpk`
2. Prepare `ux0:data/EternalLegacy/` as shown above
3. Launch the game; touch the screen on the title screen, then it loads and plays the intro

## Controls

The game itself is touch-only (its Android build ignores physical keys), so the port
converts buttons into screen touches:

| Input | Action |
|---|---|
| Touchscreen | passed through as-is |
| Left stick / D-pad | on-screen virtual stick (movement) |
| Cross | action/interact button at the lower right |
| Start | pause button at the upper right |
| Circle | back (Android BACK) |

The screen coordinates are in the tables at the top of `source/controls.c` if you need to adjust them.

## Building

```bash
export VITASDK=/path/to/vitasdk      # soft-float SDK required
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j
```

`VITASDK` points to the VitaSDK root directory.

| CMake option | Default | Description |
|---|---|---|
| `DATA_PATH` | `ux0:data/EternalLegacy/` | data directory (trailing `/` required) |
| `PERF_DIAGNOSTICS` | `ON` | print per-second performance summaries to `sceClibPrintf` |
| `VITA_MSAA` | `0` | multisampling: `0`, `2`, or `4` |

Logging is console-only; `DEBUG_LOGFILE=ON` is rejected. `[PERF]` reports frame
input/render/swap times in microseconds, FPS as `fps_x100 / 100`, frame-time
buckets, resource and libc I/O, shader compile/link, submitted texture bytes,
heap usage and free vitaGL pool memory (every five seconds). Slow calls
(at least 20 ms) and errors are limited to four details per operation per second.
Reports also run during loading. Nested timings must not be added together;
swap includes submission/waiting/vsync and is not a GPU timer. GL counters cover
direct imports. Concurrent events can straddle adjacent reporting windows.
Disable `PERF_DIAGNOSTICS` to measure instrumentation overhead.

The Java resource loader retains four read-only descriptors. Native game fopen
uses a separate 16 MiB immutable content cache (2 MiB per-file limit), independent
FILE cursors and bounded prefetch of following dialogue voices. Startup voice
indexing uses a single physical directory inventory and original native map
helpers; verified read-only KResource callers avoid redundant zero-relative seeks.
Saves keep native file semantics. A 960×640 logical canvas fits into 816×544 at
(72,0), with physical and synthetic input adapted consistently. `[IO]` records
kernel I/O statistics and slow-call paths; `[PRESENT]` records the startup fit.
Fonts share bytes with independent faces (12 MiB); ZIP entries use an 8 MiB
decompressed cache. Only sequential BGM uses 64 KiB buffering; ZIP scans retain
native buffering to avoid seek read amplification. Cache activity and error
summaries remain enabled. The Chinese README Docker command builds
`build-docker-release/EternalLegacy.vpk` using the local VitaSDK image.

## Known limitations

- The intro uses the H.264 video bundled in the VPK, with a data-directory fallback.

## Authors

- Moqi01
- Lisen
- yizhigai

## Credits

- [TheFloW](https://github.com/TheOfficialFloW) (so_util)
- [Rinnegatamante](https://github.com/Rinnegatamante) (vitaGL, ShaRKBR33D)
- [bythos14](https://github.com/bythos14) (kubridge)

## License

MIT — see [LICENSE](LICENSE)
