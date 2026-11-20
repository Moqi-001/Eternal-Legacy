# so_loader

在 PlayStation Vita 上运行 Android 共享库（`.so`）的 ELF 加载器。最初基于 TheFloW 的 [so_util](https://github.com/TheOfficialFloW/so_util)，本仓库在其基础上进行了重构与扩展。

## 功能简介

加载 ARM Android `.so` 文件，并通过虚拟库（libc、libstdc++、libGLESv1_CM 等）解析导入符号。支持加载多个相互依赖的 `.so` 模块。

**核心特性：** 不依赖真实的 Android 系统库，而是通过 `so_module_create_virtual()` 创建**虚拟库**，由虚拟库导出所需符号来满足 `.so` 的依赖。这使得 Android 共享库无需 Android 系统即可在 PS Vita 上运行。

需要 [kubridge](https://github.com/bythos14/kubridge) 提供 RWX 内存，以及 [libshacccg](https://github.com/Rinnegatamante/ShaRKBR33D) 支持 vitaGL。

## 用法

```c
// 1. 初始化链接器和 JNI
so_linker_init();
so_jni_init();

// 2. 创建虚拟库模块
so_module *c_mod      = so_module_create_virtual(&libc_so);
so_module *stdcpp_mod = so_module_create_virtual(&libstdcxx_so);

// 3. 加载依赖库
so_module *dep_mod = so_module_open("ux0:data/libdep.so");

// 4. 加载主库
so_module *main_mod = so_module_open("ux0:data/libmain.so");

// 5. Hook 函数
so_hook_ref hook_ref1, hook_ref2;
so_module_hook_address(main_mod, 0x1234, &repl_func, &hook_ref1);
so_module_hook_export(main_mod, "export_func", &repl_func2, &hook_ref2);

// 6. 调用导出函数
int (*JNI_OnLoad)(JavaVM*, void*) = (int (*)(JavaVM*, void*))so_module_find_export(main_mod, "JNI_OnLoad");
if (JNI_OnLoad) {
    JNI_OnLoad(&_java_vm, NULL);
}
```

### 创建虚拟库

```c
static so_symbol my_symbols[] = {
    SO_SYMBOL_FUNC("function", &function),
    SO_SYMBOL_OBJECT("object", &object, sizeof(object)),
};

so_library my_library = {
    .soname      = "libmylib.so",
    .symbols     = my_symbols,
    .num_symbols = sizeof(my_symbols) / sizeof(my_symbols[0]),
    .prepare     = NULL,
};

so_module *mod = so_module_create_virtual(&my_library);
```

用 `SO_UNIMPLEMENTED(name, ret)` 可生成具名桩，被调用时日志会报出是哪个符号被调用
（声明见 `so_common/so_utils.h`，用法见 `docs/PORTING.md`）。

要让 so_libs 里一些函数得到不同的行为，可用 wrap 把 `open_soloader`、`fopen_soloader`
这类函数替换成你自己的实现，不需要也不建议去改 so_libs 下的默认函数实现源码
（详情见 `docs/PORTING.md` §3.4）。

### 保留原函数的 Hook

```c
so_hook_ref hook_ref;

int repl_func(int a1, int a2) {
    // 调用原函数前执行自定义代码
    int ret = SO_CONTINUE(int, &hook_ref, a1, a2);
    // 调用原函数后执行自定义代码
    return ret;
}

so_module_hook_export(mod, "target_func", &repl_func, &hook_ref);
```

## 目录结构

```
so_loader/                ← 既是加载器源码，也是一个可直接编译的模板工程
├── vendor/               # 加载器（复用它，不在这里改）
│   ├── so_linker/        #   ELF 加载、符号解析、hook、内存
│   ├── so_libs/          #   虚拟 Android 库（libc、libstdc++、libGLESv2 等）
│   ├── so_java/          #   通用 Java 类（java/*、android/*，可复用）
│   ├── so_jni/           #   JNI 支持
│   └── so_common/        #   日志、共享缓冲等公共设施
├── source/               # 移植者写的代码：main/loader/java/patch 入口 + 各自子目录
├── libs/                 # 预编译库（vitaGL、freetype、speexdsp…）与头文件
├── resources/            # 随包分发的资源（cpuinfo/meminfo/urandom、LiveArea）
├── extras/               # 可选组件（so_font：字体渲染）
├── docs/                 # 文档
└── CMakeLists.txt        # 模板构建脚本（改 VITA_* 与 DATA_PATH 即可编）
```

> **复制即用**：把整个仓库复制成 `<游戏名>/`（去掉 `.git/`），只改 `source/` 和 `CMakeLists.txt`
> 里标了「改这里」的几项，就能编出 `.vpk`。

> **做新移植先读 [`docs/PORTING.md`](docs/PORTING.md)**：项目目录怎么摆、启动流程的调用顺序、
> Java 类与补丁该写在哪里、hook 的硬性约定，以及实机 / Vita3K 两套构建的差异。

## API 概览

### 链接器

| 函数 | 说明 |
| -------- | ----------- |
| `so_linker_init()` | 初始化动态链接器 |
| `so_linker_fini()` | 清理动态链接器（自动释放所有已注册模块） |
| `so_linker_find_module(soname)` | 按 soname 查找已注册模块 |

### Hook 类

| 函数 | 说明 |
| -------- | ----------- |
| `so_hook(target, repl, ref)` | 通过改写入口指令对 `target` 处的函数进行 inline hook（Thumb 函数需传入 `target \| 1`） |
| `so_hook_got(slot, repl, ref)` | 通过覆盖 `slot` 处 GOT 槽中存放的函数指针来 hook 调用；不修改目标函数本身 |
| `so_unhook(ref)` | 取消 hook，恢复原指令 |
| `so_inject_data(target, data, len)` | 在绝对地址处修改数据 |
| `SO_CONTINUE(type, ref, ...)` | 在 hook 内调用原函数 |

### 内存类

| 函数 | 说明 |
| -------- | ----------- |
| `so_mmap(length)` | 分配 RWX 内存 |
| `so_munmap(addr)` | 释放由 `so_mmap` 分配的内存 |
| `so_mprotect(addr, length, prot)` | 修改内存保护属性 |
| `so_cacheflush(addr, length)` | 刷新指令/数据缓存 |

### 模块生命周期

| 函数 | 说明 |
| -------- | ----------- |
| `so_module_create_virtual(lib)` | 从虚拟库创建模块（每个库只调用一次） |
| `so_module_open(path)` | 从文件路径加载、重定位并初始化 `.so` |
| `so_module_open_mem(buf, size)` | 从内存缓冲区加载、重定位并初始化 `.so` |
| `so_module_close(mod)` | 关闭模块（执行 fini + unload） |
| `so_module_load(path)` | 从文件路径加载 `.so`，但不重定位、不初始化 |
| `so_module_load_mem(buf, size)` | 从内存缓冲区加载 `.so`，但不重定位、不初始化 |
| `so_module_unload(mod)` | 卸载模块（不执行 fini） |
| `so_module_relocate(mod)` | 执行 ELF 重定位 |
| `so_module_init(mod)` | 调用初始化数组中的函数 |
| `so_module_fini(mod)` | 调用终止数组中的函数 |
| `so_module_flush_caches(mod)` | 刷新模块的指令/数据缓存 |
| `so_module_retain(mod)` | 增加模块引用计数 |
| `so_module_release(mod)` | 减少模块引用计数（归零时卸载） |

### 模块符号

| 函数 | 说明 |
| -------- | ----------- |
| `so_module_find_export(mod, name)` | 查找导出符号地址 |
| `so_module_find_import(mod, name)` | 查找导入符号地址 |

### 模块 hack

| 函数 | 说明 |
| -------- | ----------- |
| `so_module_hook_address(mod, vaddr, repl, ref)` | 按模块内相对地址 vaddr 进行 hook（Thumb 函数需传入 `vaddr \| 1`） |
| `so_module_hook_export(mod, name, repl, ref)` | 按符号名 hook 导出函数 |
| `so_module_hook_import(mod, name, repl, ref)` | 按符号名 hook 导入函数 |
| `so_module_inject_data(mod, vaddr, data, len)` | 按模块内相对地址 vaddr 修改数据 |

### 模块信息

| 函数 | 说明 |
| -------- | ----------- |
| `so_module_get_name(mod)` | 获取模块 soname |
| `so_module_get_entry_point(mod)` | 获取模块入口点 |
| `so_module_get_load_base(mod)` | 获取加载基址 |
| `so_module_get_load_size(mod)` | 获取加载大小 |
| `so_module_get_load_bias(mod)` | 获取加载偏移（load bias） |

## 内置虚拟库

- `libc.so` - C 标准库
- `libstdc++.so` - C++ 标准库
- `libm.so` - 数学库
- `libGLESv1_CM.so` / `libGLESv2.so` / `libEGL.so` - OpenGL ES
- `libdl.so` - 动态加载库
- `liblog.so` - Android 日志库
- ...

## 依赖

- [kubridge.skprx](https://github.com/bythos14/kubridge)
- [libshacccg.suprx](https://github.com/Rinnegatamante/ShaRKBR33D)

## 致谢

- [TheFloW](https://github.com/TheOfficialFloW)
- [Rinnegatamante](https://github.com/Rinnegatamante)
- ...

## 许可证

MIT —— 详见 [LICENSE](LICENSE)
