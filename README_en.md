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
cmake .. && make -j
```

`VITASDK` points to the VitaSDK root directory.

| CMake option | Default | Description |
|---|---|---|
| `DATA_PATH` | `ux0:data/EternalLegacy/` | data directory (trailing `/` required) |
| `DEBUG_OBSERVE` | `OFF` | verbose engine and GL tracing |
| `DEBUG_LOGFILE` | `OFF` | also write logs to `DATA_PATH/log.txt` |

## Known limitations

- Intro mp4 is not played yet: when the engine asks for the video it is reported as
  already finished, so the game skips straight to the loading screen.

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
