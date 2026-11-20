# Eternal Legacy —— PS Vita 移植

[中文](README.md) | [English](README_en.md)

把 Android 版《Eternal Legacy》搬到 PS Vita 上运行：用 so_loader 直接加载原版
`libEternalLegacy.so`，把 Android 侧的 JNI / GLES / 音频接口换成 Vita 实现，渲染走 vitaGL。

## 运行要求

| 项 | 说明 |
|---|---|
| 硬件 | PS Vita / PSTV |
| 系统 | 3.60 或 3.65 + HENkaku/Enso |
| 插件 | [kubridge.skprx](https://github.com/bythos14/kubridge)（RWX 内存）、[libshacccg.suprx](https://github.com/Rinnegatamante/ShaRKBR33D)（vitaGL 用） |
| 游戏数据 | 自行从正版 Android APK 提取，**本仓库不含游戏数据** |

## 数据目录

默认 `ux0:data/EternalLegacy/`，可用 `-DDATA_PATH=` 改：

```
ux0:data/EternalLegacy/
├── lib/armeabi-v7a/
│   └── libEternalLegacy.so      APK 里 lib/armeabi-v7a/ 那个
├── gameloft/games/GloftELHP/    APK 配套的 sdcard 数据（file*.dat、字体、LOGO_*.mp4 等）原样放入
└── files/                       存档目录，首次运行自动创建
```

引擎把资源路径写死成 `/sdcard/gameloft/games/GloftELHP/`、可写目录写死成
`/data/data/com.gameloft.android.TBFV.GloftELHP.ML/`，启动时分别映射到上面两个目录。

## 安装与首次运行

1. 安装 `EternalLegacy.vpk`
2. 按上面的结构准备 `ux0:data/EternalLegacy/`
3. 启动游戏，标题画面触摸屏幕进入，之后是加载画面和开场剧情

## 操作

游戏本身是纯触摸操作（安卓端不支持实体按键），移植版把实体按键换算成屏幕触摸：

| 操作 | 动作 |
|---|---|
| 触摸屏 | 直接透传 |
| 左摇杆 / 十字键 | 左下虚拟摇杆（移动） |
| Cross | 右下动作/交互键 |
| Start | 右上暂停键 |
| Circle | 返回（Android BACK） |

按键对应的屏幕坐标在 `source/controls.c` 顶部的表里，界面有出入可以直接改。

## 构建

```bash
export VITASDK=/path/to/vitasdk      # 需要 soft-float 版的 SDK
mkdir -p build && cd build
cmake .. && make -j
```

`VITASDK` 指向 VitaSDK 根目录。

| CMake 选项 | 默认 | 说明 |
|---|---|---|
| `DATA_PATH` | `ux0:data/EternalLegacy/` | 数据目录（结尾要带 `/`） |
| `DEBUG_OBSERVE` | `OFF` | 引擎与 GL 的详细观测打点 |
| `DEBUG_LOGFILE` | `OFF` | 日志另写一份到 `DATA_PATH/log.txt` |

## 已知限制

- 开场 mp4 暂未播放：引擎请求视频时直接按“已播完”上报，跳过动画进入加载画面。

## 作者

- Moqi01
- Lisen
- yizhigai

## 致谢

- [TheFloW](https://github.com/TheOfficialFloW)（so_util）
- [Rinnegatamante](https://github.com/Rinnegatamante)（vitaGL、ShaRKBR33D）
- [bythos14](https://github.com/bythos14)（kubridge）

## 许可证

MIT —— 详见 [LICENSE](LICENSE)
