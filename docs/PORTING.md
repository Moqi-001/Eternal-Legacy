# 移植项目结构使用说明（soloader 模板）

本文描述「用 so_loader 做一个 Android `.so` 移植」时的标准项目结构、启动流程、
扩展点和硬性约定。目标是：**第一次接触这个仓库的人（或 AI）读完就知道代码该写在哪里、
按什么顺序执行、以及哪些坑已经踩过**。

本仓库的**根目录就是最小骨架**（复制它就是一个新工程）；完整例子见 `ShantaePiratesCurse`
（NativeActivity 型）、`bio4`、`acrodea_runtime`（多 `.so` 型）。

---

## 1. 分层

```
项目工程（每个游戏一个）
├── source/                 ← 移植者写的代码（本文重点）
│   ├── main.c              程序入口：系统模块/时钟/对话框初始化 → soloader_init/run/term
│   ├── loader.c/.h         加载所有 .so + 驱动游戏生命周期
│   ├── java.c/.h           Java 总入口：soloader_setup_java()
│   ├── patch.c/.h          补丁总入口：soloader_patch_modules()
│   ├── java/               游戏**私有** Java 类（如 MyActivity.c）
│   ├── patch/              具体补丁实现（按 .so 分文件）
│   ├── utils/              对话框、杂项
│   └── config.h            屏幕尺寸、内存阈值、数据路径
├── vendor/                 ← so_loader 的源码快照（从模板仓库拷来）
│   ├── so_common/          logger / shared_buffer / utils
│   ├── so_linker/          ELF 加载、符号解析、hook、内存
│   ├── so_jni/             JNI 环境、局部/全局引用、调用转发
│   ├── so_java/            **通用** Java 类（java/*、android/*，可复用）
│   └── so_libs/            虚拟 libc/libm/libEGL/libGLESv2/libandroid/... 实现
├── libs/
│   ├── include/            vitaGL、freetype、kubridge 等头文件
│   └── lib/                预编译库（libvitaGL.a 等）
├── resources/              随包分发的文件（cpuinfo/meminfo/urandom、LiveArea）
├── extras/                 可选组件（so_font 字体渲染）
└── CMakeLists.txt
```

**分界线**：`vendor/` 是「模拟实现的 android 共享库代码」，`source/` 是「这个游戏的项目代码」；
`libs/` 是「预编译产物（vitaGL、freetype 等）」，和 `vendor/` 的区别是「二进制 vs 源码」。
判断一个 Java 类该放哪：**通用就放 `so_java`（别的项目通用），游戏私有就放 `source/java/`**。

---

## 2. 启动流程（顺序是硬约定）

```
main()
 ├─ sceAppUtilInit  psvita 系统的一些初始化
 ├─ soloader_init()
 │   ├─ so_linker_init()            链接器
 │   ├─ so_jni_init()               JNI 环境（局部/全局引用上下文）
 │   ├─ soloader_setup_modules()    创建虚拟库模块（libc、libGLESv2、libandroid…）
 │   ├─ soloader_setup_java()       注册所有 Java 类（通用 + 私有）
 │   ├─ so_module_open(游戏 .so)     动态加载游戏共享库
 │   ├─ soloader_patch_modules()    安装补丁与 hook（**在 .so 加载之后**）
 │   └─ soloader_init_opengl()      openGL/vitaGL 的初始化
 ├─ soloader_run()
 │   ├─ soloader_jni_onload(模块)    内部调用 .so 的 JNI_OnLoad（没有会自动跳过）
 │   ├─ 构造 ANativeActivity / 调用入口（NativeActivity 型）或 onStart 等（Activity 型）
 │   └─ 驱动生命周期 + 输入线程 + 等待退出
 └─ soloader_term()
```

要点：

- **`so_module_open` 之后才能打补丁**：`so_module_find_export()` 需要模块已重定位。
- **`vglInit` 放在补丁之后**：vitaGL 的 pool 大小 = 剩余内存 − 阈值，前面的分配会先占掉。

---

## 3. 扩展点

### 3.1 加一个 Java 类

```c
// source/java/Member.c（游戏私有；通用类请放 vendor/so_java/...）
static JSymbol class_symbols[] = {
    JAVA_SYMBOL_METHOD("getName", "()Ljava/lang/String;", &Member_getName),
    JAVA_SYMBOL_STATIC_METHOD("create", "(I)V", &Member_create),
};

static JClassMeta class_meta = {
    .name          = "com/game/Member",
    .super_name    = NULL,
    .symbols       = class_symbols,
    .num_symbols   = sizeof(class_symbols) / sizeof(class_symbols[0]),
    .instance_size = sizeof(struct _Member),  // 需要实例时**不能为 0**
};

void setup_Member(void) { sClass = JavaCreateClass(&class_meta); }
```

然后在 `java.c` 的 `soloader_setup_java()` 里注册它。

约定：

- `instance_size = 0` 表示「纯静态占位类」；要给游戏返回真实 `jobject` 时必须 > 0。
- 方法返回 `JObject` 时**要先 `Object_retain`**（`JniCallCommonMethodV` 会消费这个引用）。
- 找不到的类/方法返回 NULL 可能会让游戏崩溃，建议按需补类方便后面追踪，不建议修改so_jni源码用假指针行为（如 `0x1`）。

### 3.2 加一个补丁 / hook

补丁少时可直接写在 `patch.c` 里；多了可按模块拆到 `patch/<模块名>.c`（下例），
`patch.c` 只保留分发，并把模块指针传下去（`patch/` 里不需要引用 loader 的全局变量）。

```c
// source/patch/<模块名>.c
static so_hook_ref s_hk_foo;

static void hook_foo(void *self, int a) {
    so_loga("[PATCH] foo(%d)", a);
    SO_CONTINUE(void, &s_hk_foo, self, a);   // 调原函数
}

int soloader_patch_<模块名>(so_module *mod) {   // 模块指针由 patch.c 传入
    so_module_hook_export(mod, "_ZN4Game3fooEi", (void *)hook_foo, &s_hk_foo);
    return 0;
}
```

对应的 `patch.c`：

```c
#include "loader.h"        // 模块全局指针由 loader 持有
#include "patch.h"
#include "patch/<模块名>.h"

int soloader_patch_modules() {
    return soloader_patch_<模块名>(<模块名>_mod);   // 多 .so 就多次调用
}
```

**硬性规则：hook 的替换函数签名与返回类型必须与原函数完全一致。**
返回类型写错会破坏调用方拿到的 `r0`：真实事故是 `FileRef::get()` 被声明成 `void`，
调用方拿到 NULL 后崩在 `0x0`；`isLoaded()` 同理导致流程判断错乱。

调用原函数统一用 `SO_CONTINUE(type, &hook_ref, ...)`，不用手写函数指针强转。

**只改行为、不记录时**可以不打日志；打点日志可用 `so_loga` 可无视日志宏（见 §4 日志）。

### 3.3 加一个虚拟库（新的 `lib*.so` 依赖）

1. 在 `vendor/so_libs/<name>/` 下写实现 + `so_library <name>_so = { .soname = "lib<name>.so", .symbols = ... }`；
2. 在 `loader.c` 里 `extern so_library <name>_so;` 并在 `soloader_setup_modules()` 里 `so_module_create_virtual(&<name>_so)`。

**未实现的符号不建议共用一个空桩**（几个符号指向同一个 `int f(void) { return 0; }`）：被调用时
无从知道是哪个函数。用 `so_common/so_utils.h` 里的宏为每个符号生成具名桩（日志里直接报出符号名）：

```c
// 普通符号名：宏直接拿函数名当日志标签
SO_UNIMPLEMENTED(android_reboot, -1)                   // 定义 int android_reboot_soloader(void)
SO_SYMBOL_FUNC("android_reboot", &android_reboot_soloader)

// 想让日志报出游戏导入的原始符号名（mangled）：标签显式给
SO_UNIMPLEMENTED_LABELED(android_IBinder_pingBinder, "_ZN7android7IBinder14pingBinderEv", 0)
SO_SYMBOL_FUNC("_ZN7android7IBinder14pingBinderEv", &android_IBinder_pingBinder_soloader)
```

两个形态只有日志标签的来源不同：普通版取 C 标识符，`_LABELED` 版取你给的字符串——
排障时能直接拿它去 `readelf`/`nm` 里搜导出符号。最后一个参数是桩要返回的值，
按调用方语义给：成功/失败标记给 `0` 或 `-1`，句柄给 `-1` 或 `0`（NULL）。
桩被调用时输出 `"<标签>() not implemented, returning <值>"`。

这几个宏是**调试初期的普查手段**：未实现的符号先全量套上，跑起来看日志就能分成两类——
真被游戏调用的（要补实现）和只为满足链接、从未被调用的（留桩即可）。未实现符号通常很多，
逐个补不现实，先普查再补。某个符号的日志一直刷，说明它在热路径上，该补真实实现了，
不要靠关日志掩盖。已有 `xxx_soloader` 真实实现的函数不能再套宏（名字会撞）。

### 3.4 用自己的实现替换 libc 函数（wrap `xxx_soloader`）

**不需要也不建议修改 `vendor/so_libs/libc` 下的函数实现源码**——那份代码是所有项目共用的，
改了以后同步加载器就会冲突。要让某个游戏得到不同的行为，用 wrap 把 `open_soloader`、
`fopen_soloader` 这类函数替换成你自己的实现：写在 `source/patch/` 里，先做自己的处理，
再决定要不要交给原实现。

**为什么换的是 `xxx_soloader` 而不是 `open` 本身**：

| 谁在调 `open` | 落到哪个实现 |
|---------------|--------------|
| 游戏 `.so` | `libc_so` 符号表里 `"open"` → **`open_soloader`**（移植层实现） |
| 你自己的主程序（`source/`） | newlib 的真 `open`（静态链接进来，和虚拟 libc 无关） |

`open_soloader` 就是为「可以按项目替换」而存在的这一层；而真 `open` 是主程序在用的，
去 hook 它会把主程序一起改掉。所以目标是 `open_soloader`。

```c
// source/patch/<模块名>.c
#include <fcntl.h>
#include <stdarg.h>

#include <so_linker/so_linker.h>

static so_hook_ref s_hk_open;

// 签名与 open_soloader 一致（可变参数：带 O_CREAT 时才有第 3 个参数）
static int hook_open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }

    // 自己的处理：例如把 '/data/...' 重定向到 'ux0:data/...'
    return SO_CONTINUE(int, &s_hk_open, path, flags, mode);   // 调回 open_soloader
}

int soloader_patch_<模块名>(so_module *mod) {
    // 只改这个 .so 的导入槽（它 GOT 里那个指针），游戏侧生效，别处不受影响
    so_module_hook_import(mod, "open", (void *)hook_open, &s_hk_open);
    return 0;
}
```

要点：

- hook 时写的是**符号名** `"open"`，但它指向的实现就是 `open_soloader`——这就是「wrap 的是
  `open_soloader`，不是 `open`」。
- 用的是 `hook_import`（改导入槽里的指针），不是 `hook_export`：见下一条。
- **四个 hook 入口（`address`/`export`/`import`/`inject_data`）都只接受真实加载的 `.so`**。
  虚拟库（`libc.so` 这种 `so_module_create_virtual` 出来的）没有加载镜像，它的「导出」只是符号表里
  登记的函数指针，指向 `open_soloader` 或 newlib 的 `open`：内联 hook 会去改那段代码——要么
  改掉所有模块共用的实现，要么写只读代码段直接死机（`so_hook` 不改页权限）。对虚拟库的调用
  一律返回 -1 并报 `'xxx' is a virtual library`。
- 失败原因都会打日志，注意区分：`'xxx' is a virtual library`（这个库不该 hook）／
  `'xxx' is not imported by 'libyyy.so'`（符号名写错，或该 `.so` 没导入它）／
  `export 'xxx' not found in 'libyyy.so'`（模块对了但没这个导出）。
- `SO_CONTINUE` 用 `so_hook_ref.orig_func` 调原实现，是「包一层」；确实要完全替换就直接返回
  自己的值，不调 `SO_CONTINUE`。
- `fopen_soloader`（`"fopen"`）、`stat_soloader`（`"stat"`）同理；不是 `xxx_soloader` 的函数
  （如 `"access"` → `access`）说明它没做移植层封装，这类通常也不需要按项目改。
- 改动放 `source/patch/`，`vendor/` 与上游保持一致，随时能整体替换。

---

## 4. 硬性约定

| 约定 | 说明 |
|------|------|
| 模块全局指针按 `.so` 命名 | 单 `.so` 用 `shantae_mod`；多 `.so` 每个模块一个（`mce_mod`、`bio4_mod`…） |
| 不改 `vendor/` | `vendor/so_libs/libc` 是全项目共用的实现，要换就 wrap `xxx_soloader` 写在 `source/patch/`，见 §3.4 |
| 只 hook 真实 `.so` | 虚拟库（`so_module_create_virtual` 出来的）没有镜像，四个 hook 入口一律返回 -1 并报错；改 libc 行为请用 `hook_import` 改游戏侧导入槽（§3.4） |
| 补丁入口只暴露总函数 | `patch.h` 只有 `soloader_patch_modules()`（+ 诊断查询）；实现在 `patch/<模块>.c` |
| Java 入口只暴露总函数 | `java.h` 只有 `soloader_setup_java()` |
| 日志分级 | 阈值 `SO_LOG_LEVEL`（1=VERBOSE…5=ERROR）决定 `so_logv/d/i/w` 是否编进来；`so_loge` 始终、`so_logf` 致命退出、`so_loga` 无视阈值 |
| 热路径不埋点 | 每次分配/每次 draw 不打日志；实机 `sceClibPrintf` 极慢，逐调用日志能把 60fps 拖到几秒一帧 |
| 诊断用 `so_loga`，别用 `so_loge` | 否则看起来像真出错 |
| 注释风格 | 文件头和较长的说明块（4 行以上）用 `/* */`；短注释、行尾注释用 `//` |

日志阈值：不定义 `SO_LOG_LEVEL` 时，有 `-DDEBUG_SOLOADER` 按 DEBUG、否则按 ERROR——和以前的
行为一致。要追 malloc、着色器编译这类逐次探测，用 `-DSO_LOG_LEVEL=1` 配合 `so_logv`。

---

## 5. 构建矩阵（vitaGL 的两种形态）

| 目标 | 需求 | vitaGL 构建 |
|------|------|-------------|
| **实机** | 需要 `SCE_GXM_INITIALIZE_FLAG_EXTENDED_FORMAT`（否则 ETC1/UBC 纹理描述符非法，**所有 draw 被跳过**、黑屏） | `ENABLE_VITA3K_COMPAT=OFF` + `DISABLE_HW_ETC1=OFF`（默认 release 配置） |
| **Vita3K** | 模拟器不支持 `sceGxmVshInitialize` | `ENABLE_VITA3K_COMPAT=ON`（隐含 `DISABLE_HW_ETC1`，ETC1 走软件解码，2048² 图集从 2MB 膨胀到 12MB，很吃内存/很慢） |

结论：**同一个游戏需要两套构建**，别用实机包在模拟器上调试（Vita3K 遇到硬件 ETC1 格式会自己崩）。

`source/config.h` 关键项：

```c
#define MEMORY_NEWLIB_MB 256        // 引擎堆上限（malloc）
#define VITAGL_RAM_THRESHOLD_MB 20  // 留给别人多少 RAM（vitaGL pool = 剩余 − 阈值）
#define VITAGL_CDRAM_THRESHOLD_MB 4
```

堆与 vitaGL 池是**此消彼长**：堆给多了，vitaGL 池不够会 `gpu_alloc_*` 失败（表现是纹理黑）；
堆给少了，引擎 `bad_alloc`。两边都要给够。

---

## 6. CMakeLists 写法

三个 glob 区块，各自解释：

```cmake
enable_language(ASM)                     # so_jni / so_libs 里有 .S

# 框架（必需）
file(GLOB_RECURSE SO_COMMON_SRC vendor/so_common/*.c)
file(GLOB_RECURSE SO_LINKER_SRC vendor/so_linker/*.c)
file(GLOB_RECURSE SO_JAVA_SRC   vendor/so_java/*.c vendor/so_java/*.cpp)
file(GLOB_RECURSE SO_JNI_SRC    vendor/so_jni/*.c vendor/so_jni/*.S)

# 虚拟库：默认**全部**编进来（一次构建就能验证整份 vendor 源码能否编过），
# 移植时再删掉用不到的（看 .so 的 DT_NEEDED，以及 loader.c 里 so_module_create_virtual() 了哪些）
file(GLOB_RECURSE SO_LIBC_SRC        vendor/so_libs/libc/*.c)
file(GLOB_RECURSE SO_LIBM_SRC        vendor/so_libs/libm/*.c)
file(GLOB_RECURSE SO_LIBGLESV2_SRC   vendor/so_libs/libGLESv2/*.c)
# ...每个 vendor/so_libs/<name>/ 一行，完整清单见仓库 CMakeLists.txt

# 暂时没用到的库按需注释掉（如 SDL 系、libsmpeg2、extras/so_font）
#file(GLOB_RECURSE SO_LIBSDL2_SRC  vendor/so_libs/libSDL2/*.c)
#file(GLOB_RECURSE SO_FONT_SRC    extras/so_font/*.c)

# 项目代码：source/ 整个目录一起收（入口与 java/、patch/、utils/ 子目录都进同一个可执行文件）
file(GLOB_RECURSE SOURCE_SRC source/*.c)
```

`add_executable` 里把这三组变量按组列出（一个变量一行），空变量（如未启用的
`${SO_FONT_SRC}`）不会影响构建。**完整清单以仓库根目录的 `CMakeLists.txt` 为准**，本文不重复。

`speexdsp` 和 `sqlite3` 别从链接列表里删：`so_java` 的 `AudioTrack`、`vendor/so_libs/libsqlite` 都要用。

`source/` 用 glob 的好处是加文件不用改 CMakeLists；代价是两点：新增/删除文件后要重跑一次
configure（见本节末尾），以及**别把不打算编译的 `.c` 放进 `source/`**（会被一起收进去）。

头文件与库路径：

```cmake
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
  PUBLIC ${CMAKE_SOURCE_DIR}/libs/include
  PUBLIC ${CMAKE_SOURCE_DIR}/libs/include/freetype2
  PUBLIC ${CMAKE_SOURCE_DIR}/vendor           # <so_linker/so_linker.h> 等
  PUBLIC ${CMAKE_SOURCE_DIR}/vendor/so_libs   # <libandroid/input.h> 等
  PUBLIC ${CMAKE_SOURCE_DIR}/extras           # <so_font/so_font.h> 等（用到 extras 时）
)
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/source)

target_link_directories(${CMAKE_PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/libs/lib)
target_link_libraries(${CMAKE_PROJECT_NAME}
  -Wl,--whole-archive pthread -Wl,--no-whole-archive   # pthread 必须 whole-archive
  vitaGL vitashark SceShaccCgExt taihen_stub SceShaccCg_stub kubridge_stub
  mathneon speexdsp sqlite3 freetype png bz2 z m stdc++
  SceHid_stub SceCtrl_stub SceTouch_stub SceMotion_stub SceAppMgr_stub
  SceAppUtil_stub SceAudio_stub SceAudioIn_stub SceCommonDialog_stub
  SceDisplay_stub SceKernelDmacMgr_stub SceGxm_stub ScePower_stub
  SceVshBridge_stub SceAvPlayer_stub
)
```

打包（`VITA_*` 变量先设好）：

```cmake
set(VITA_APP_NAME "...")
set(VITA_TITLEID  "XXXXXXXX")   # 9 位
set(VITA_VPKNAME  "GameName")
set(VITA_VERSION  "01.00")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE NOASLR)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
  # 根目录这三个对应 libc 对 Android 的模拟：/proc/cpuinfo、/proc/meminfo、/dev/urandom
  FILE ${CMAKE_SOURCE_DIR}/resources/cpuinfo    cpuinfo
  FILE ${CMAKE_SOURCE_DIR}/resources/meminfo    meminfo
  FILE ${CMAKE_SOURCE_DIR}/resources/urandom    urandom
  # 对应 config.h 的 SO_TTF_PATH
  FILE ${CMAKE_SOURCE_DIR}/resources/font.ttf   font.ttf
  # 气泡图标与 LiveArea
  FILE ${CMAKE_SOURCE_DIR}/resources/livearea/icon0.png    sce_sys/icon0.png
  FILE ${CMAKE_SOURCE_DIR}/resources/livearea/pic0.png     sce_sys/pic0.png
  FILE ${CMAKE_SOURCE_DIR}/resources/livearea/startup.png  sce_sys/livearea/contents/startup.png
  FILE ${CMAKE_SOURCE_DIR}/resources/livearea/bg0.png      sce_sys/livearea/contents/bg0.png
  FILE ${CMAKE_SOURCE_DIR}/resources/livearea/template.xml sce_sys/livearea/contents/template.xml
)
```

`cpuinfo`/`meminfo`/`urandom` 是 `so_libs/libc` 里 `/proc/cpuinfo`、`/proc/meminfo`、`/dev/urandom`
的重定向目标（`app0:/xxx`），漏打进去会让引擎读这些文件失败。仓库的 `resources/` 里都备好了。

注意：`vita_create_self` / `vita_create_vpk` 是 **macro**（没有独立作用域），会往
`VITA_*_FLAGS` 这些普通变量上累积。一个项目里多次调用（多游戏工程）时，后面的产物会
继承前面的标志——需要时用 `function()` 包一层，或用后 `unset()` 相关变量。

`file(GLOB ...)` 在 **configure 时**展开：新增或删除源文件后要重跑一次 `cmake`（在 build
目录里执行 `cmake .`，或 `touch CMakeLists.txt` 再 make），否则 Makefile 还指向旧文件列表。

浮点 ABI：

```cmake
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -mfloat-abi=softfp -std=gnu11")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfloat-abi=softfp -std=gnu++20")
```

必须与所用 vitaGL 的 ABI 一致（软浮点 SDK 默认就是 softfp）。

**起点直接用仓库根目录的 `CMakeLists.txt`**：它默认打开 `libc/libm/libstdcxx/liblog/libdl/libEGL/
libGLESv1_CM/libGLESv2`，其余虚拟库按注释逐行打开（`source/loader.c` 里 `so_module_create_virtual()`
了哪个就开哪个）。`libs/` 里的 `speexdsp` 被 `so_java` 的 `AudioTrack` 用到，不能省。

## 7. 已踩过的坑（按表现查）

| 现象 | 原因 | 处置 |
|------|------|------|
| 黑屏，日志有 `Fragment 2D texture on TEXUNIT0 is invalid` / `draw will be skipped` | GXM 缺 `EXTENDED_FORMAT`，ETC1 描述符非法 | 实机构建关掉 `ENABLE_VITA3K_COMPAT` |
| 卡在某个界面不推进、但帧在刷 | 该界面的等待条件没满足（引擎内部状态机 / 异步资源未就绪 / 过场计时） | 在该界面的入口和它忙等的判定函数上 `so_module_hook_export` 打点（进入、离开各一条），看最后一个「只进不出」的调用 |
| `epoll_ctl` 对管道 fd 返回 EBADF | `sceNetEpoll` 只接受 socket；newlib `pipe()` 是 msgpipe | 本项目 epoll 是用户态轮询：msgpipe 用 `sceKernelGetMsgPipeInfo` 查 `freeSize`，socket 用 `recv(PEEK\|DONTWAIT)` |
| `fcntl(F_SETFL, O_NONBLOCK)` 对管道失败 | newlib `_fcntl_r` 只处理 FILE/SOCKET，PIPE 直接 EBADF；`read/write` 也从不看 flags | `bionic_fcntl.c` 自管 msgpipe 标志；`read/write` 置位时改走 `TryReceive/TrySend`（注意 Try 版 `unk1` 必须为 0） |
| 启动卡在音频初始化后 | 引擎在等 buffer-queue 回调 | OpenSL ES 桩要真正回调：`RegisterCallback` 存下 cb，用 16ms 周期线程驱动 |
| 帧率几秒一帧 | 逐调用日志（vitaGL `LOG_LEVEL=2` + `DEBUG_SOLOADER`） | 发布构建用 `LOG_LEVEL=1`、`CMAKE_BUILD_TYPE=Release` |
| hook 后必崩在 `0x0` / 流程判断错乱 | hook 替换函数返回类型写错 | 见 §3.2 |
| 进到某段流程就 `abort`，看不到异常信息 | `.so` 里的 C++ 异常需要 `.ARM.exidx`；`__gnu_Unwind_Find_exidx` 若返回 0，展开必然失败，`__cxa_throw` 直接 terminate | 模板已按 PC 返回真实异常表（`so_linker_find_exidx`），不要把它当普通桩改回返回 0 |
| 跳过某个「要加载大卷」的引擎函数后，进关卡崩在 `std::out_of_range` | 引擎会把加载过的卷推进一张表，别处对这张表取首元素/遍历，表空了就抛异常 | 关掉那个函数的同时，把它后续依赖的「卷计数 / 卷清单」类函数一起兜住（返回合理值或跳过纯打印） |

---

## 8. 新项目 checklist

1. 把整个仓库复制成 `<游戏名>/`（去掉 `.git/`），它已经是可编译的骨架：
   - `source/config.h` 改屏幕尺寸、内存阈值；
   - `CMakeLists.txt` 改 `VITA_APP_NAME`/`VITA_TITLEID`/`VITA_VPKNAME`、`DATA_PATH`，
     并按需打开虚拟库。
2. `vendor/so_libs/` 默认全部参与构建（一次就能验证源码可编过），移植时再删掉用不到的；
   `libs/` 已带 vitaGL 等预编译库，换 vitaGL 构建时替换 `libs/lib/libvitaGL.a`。
3. `source/loader.c`：
   - `soloader_setup_modules()` 列出游戏实际依赖的虚拟库（看 `.so` 的 `DT_NEEDED`）；
   - 用 `.so` 名命名的全局模块指针；
   - `soloader_init` 的调用顺序按 §2。
4. 跑一次，看第一个失败点：
   - `so_module_open` 失败 → 缺虚拟库或缺符号；
   - 进游戏但黑屏 → 查 `gpu_alloc`/`draw will be skipped`/纹理格式；
   - 停在某界面 → 从日志里最后一条业务输出反推到它所在的函数，再沿调用者一路上溯到「场景/状态机入口」，逐层 `so_module_hook_export`（进入、离开各一条），最后那个只进不出的就是卡点。
5. 按需补 Java 类与补丁；每加一个 hook 先确认返回类型。
6. 实机与 Vita3K 用两套 vitaGL 构建，分别验证。
