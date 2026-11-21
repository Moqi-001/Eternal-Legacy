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
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j
```

`VITASDK` 指向 VitaSDK 根目录。

| CMake 选项 | 默认 | 说明 |
|---|---|---|
| `DATA_PATH` | `ux0:data/EternalLegacy/` | 数据目录（结尾要带 `/`） |
| `PERF_DIAGNOSTICS` | `ON` | 每秒汇总性能指标到 `sceClibPrintf`，不写日志文件 |
| `VITA_MSAA` | `0` | MSAA 采样数，可选 `0`、`2`、`4` |

性能日志带 `[PERF]` 前缀：`frame` 记录 FPS（`fps_x100 / 100`）、输入/
nativeRender/swap 平均耗时及帧耗时分布，单位为微秒；`op` 记录资源和 libc I/O、
shader 编译/link、纹理上传的调用数、字节数、耗时与失败数；`heap` 与
`gpu_free_kb` 每 5 秒记录堆和 vitaGL 内存池。慢调用（≥20ms）及错误详情
按操作每秒最多 4 条。长加载期间也会输出汇总。
各层耗时可能嵌套，不能直接相加；swap 包含提交/等待/垂直同步，不等于纯 GPU 耗时。
GL 计数覆盖游戏直接导入的入口，纹理字节数描述提交量，未进行 GPU 完成计时。
多线程计数在每秒边界可能分到相邻窗口。关闭 `PERF_DIAGNOSTICS` 可测量观测本身的开销。

Java GLResLoader 复用最多 4 个只读资源句柄；实际游戏使用的原生 fopen 路径另有
16 MiB 只读内容缓存、独立 FILE 游标和后续对话语音预取，单文件上限 2 MiB。
启动语音存在性扫描改用一次目录清单和原库 voiceMap；KResource 的冗余 seek
仅在经过原指令验证的只读调用点消除。存档保持原生文件语义。
完整游戏使用 960×640 逻辑画布，居中显示为 816×544，左右各 72 像素黑边。
`[IO]` 保留内核 I/O 统计和慢调用路径；`[PRESENT]` 仅记录启动适配配置。
字体使用 12 MiB 共享字节和独立 face，ZIP 解压条目缓存上限 8 MiB。64 KiB
缓冲只用于确认的顺序 BGM，ZIP 扫描保留原生缓冲，避免跳读放大。
`[FONT]`、`[PACK_CACHE]`、`[SHADER_CACHE]` 保留活动、错误及缓存命中汇总。
早期补丁背景与历史日志见 [01.01 修复说明](docs/performance-fix-20261121.md)。
工程使用纯控制台 logger，旧 `DEBUG_LOGFILE=ON` 配置会被拒绝。

本地 Docker 构建（使用已有镜像，无需拉取）：

```bash
docker run --rm --platform linux/amd64 --network none \
  -v "$PWD:/workspace" -w /workspace vitasdk/vitasdk-artemis:latest \
  sh -c 'cmake -S . -B build-docker-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DPERF_DIAGNOSTICS=ON -DDEBUG_LOGFILE=OFF -DVITA_MSAA=0 && cmake --build build-docker-release -j4'
```

产物为 `build-docker-release/EternalLegacy.vpk`。宿主回归入口包括：
`python3 tests/perf_review.py`、`python3 tests/native_io_review.py`、
`python3 tests/io_trace_review.py`、`python3 tests/presentation_review.py`、
`python3 tests/resource_index_test.py` 和 `python3 tests/voice_index_test.py`。

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
