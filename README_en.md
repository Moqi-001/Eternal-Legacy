# so_loader

ELF loader for running Android shared libraries (`.so`) on the PlayStation Vita. Originally based on [so_util](https://github.com/TheOfficialFloW/so_util) by TheFloW. This repo refactors and extends the original work.

## What it does

Loads ARM Android `.so` files and resolves imports through virtual libraries (libc, libstdc++, libGLESv1_CM, etc.). Supports loading multiple interdependent `.so` modules.

**Key feature:** Instead of relying on actual Android system libraries, `so_loader` creates **virtual libraries** via `so_module_create_virtual()` that export the necessary symbols to satisfy the `.so`'s dependencies. This allows Android shared libraries to run on PS Vita without the Android OS.

Requires [kubridge](https://github.com/bythos14/kubridge) for RWX memory and [libshacccg](https://github.com/Rinnegatamante/ShaRKBR33D) for vitaGL support.

## Usage

```c
// 1. Initialize the linker and JNI
so_linker_init();
so_jni_init();

// 2. Create virtual library modules
so_module *c_mod      = so_module_create_virtual(&libc_so);
so_module *stdcpp_mod = so_module_create_virtual(&libstdcxx_so);

// 3. Load dependent libraries
so_module *dep_mod = so_module_open("ux0:data/libdep.so");

// 4. Load main library
so_module *main_mod = so_module_open("ux0:data/libmain.so");

// 5. Hook functions
so_hook_ref hook_ref1, hook_ref2;
so_module_hook_address(main_mod, 0x1234, &repl_func, &hook_ref1);
so_module_hook_export(main_mod, "export_func", &repl_func2, &hook_ref2);

// 6. Call exported functions
int (*JNI_OnLoad)(JavaVM*, void*) = (int (*)(JavaVM*, void*))so_module_find_export(main_mod, "JNI_OnLoad");
if (JNI_OnLoad) {
    JNI_OnLoad(&_java_vm, NULL);
}
```

### Creating a virtual library

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

`SO_UNIMPLEMENTED(name, ret)` creates a named stub — when it is called, the log reports which
symbol was hit (declared in `so_common/so_utils.h`, usage in `docs/PORTING.md`).

To give some functions in so_libs different behaviour, wrap `open_soloader`, `fopen_soloader`
and the like with your own implementation. There is no need to — and you should not — edit the
default implementations under so_libs (details in `docs/PORTING.md` §3.4).

### Hooking with original function preservation

```c
so_hook_ref hook_ref;

int repl_func(int a1, int a2) {
    // Custom code before calling original
    int ret = SO_CONTINUE(int, &hook_ref, a1, a2);
    // Custom code after calling original
    return ret;
}

so_module_hook_export(mod, "target_func", &repl_func, &hook_ref);
```

## Directory Structure

```
so_loader/                # The loader itself, and a directly buildable template project
├── vendor/               # The loader (reuse it, don't edit here)
│   ├── so_linker/        #   ELF loading, symbol resolution, hooks, memory
│   ├── so_libs/          #   Virtual Android libraries (libc, libstdc++, libGLESv2, ...)
│   ├── so_java/          #   Shared Java classes (java/*, android/*, reusable)
│   ├── so_jni/           #   JNI support
│   └── so_common/        #   Logging, shared buffers and other common utilities
├── source/               # Your code: main/loader/java/patch entry points + subdirs
├── libs/                 # Prebuilt libraries (vitaGL, freetype, speexdsp, ...) and headers
├── resources/            # Files shipped in the vpk (cpuinfo/meminfo/urandom, LiveArea)
├── extras/               # Optional components (so_font: font rendering)
├── docs/                 # Documentation
└── CMakeLists.txt        # Template build script (set VITA_* and DATA_PATH and build)
```

> **Copy and go**: copy the whole repo as `<GameName>/` (drop `.git/`), edit `source/` and the
> items marked with a comment in `CMakeLists.txt`, and you get a `.vpk`.

> **Read [`docs/PORTING.md`](docs/PORTING.md) first when starting a port**: how to lay out the
> project, the required startup call order, where Java classes and patches belong, the hard rules
> for hooks, and the difference between the hardware and Vita3K builds.

## API Overview

### Linker

| Function | Description |
| -------- | ----------- |
| `so_linker_init()` | Initialize the dynamic linker |
| `so_linker_fini()` | Cleanup the dynamic linker (automatically releases all registered modules) |
| `so_linker_find_module(soname)` | Find a registered module by soname |

### Hack

| Function | Description |
| -------- | ----------- |
| `so_hook(target, repl, ref)` | Inline-hook the function at `target` by rewriting its entry instructions (pass `target \| 1` for Thumb) |
| `so_hook_got(slot, repl, ref)` | Hook calls through the function pointer stored in a GOT slot at `slot`; the target function itself is not modified |
| `so_unhook(ref)` | Unhook, restore original instructions |
| `so_inject_data(target, data, len)` | Patch data at an absolute address |
| `SO_CONTINUE(type, ref, ...)` | Call the original function from within a hook |

### Memory

| Function | Description |
| -------- | ----------- |
| `so_mmap(length)` | Allocate RWX memory |
| `so_munmap(addr)` | Free memory allocated by `so_mmap` |
| `so_mprotect(addr, length, prot)` | Change memory protection |
| `so_cacheflush(addr, length)` | Flush instruction/data caches |

### Module lifecycle

| Function | Description |
| -------- | ----------- |
| `so_module_create_virtual(lib)` | Create a module from a virtual library (once per library) |
| `so_module_open(path)` | Load, relocate and initialize a `.so` from a file path |
| `so_module_open_mem(buf, size)` | Load, relocate and initialize a `.so` from a memory buffer |
| `so_module_close(mod)` | Close a module (executes fini + unload) |
| `so_module_load(path)` | Load a `.so` from a file path without relocation and initialization |
| `so_module_load_mem(buf, size)` | Load a `.so` from a memory buffer without relocation and initialization |
| `so_module_unload(mod)` | Unload a module (without executing fini) |
| `so_module_relocate(mod)` | Perform ELF relocation |
| `so_module_init(mod)` | Call initialization array functions |
| `so_module_fini(mod)` | Call termination array functions |
| `so_module_flush_caches(mod)` | Flush instruction/data caches for the module |
| `so_module_retain(mod)` | Increase module reference count |
| `so_module_release(mod)` | Decrease module reference count (unloads when zero) |

### Module symbols

| Function | Description |
| -------- | ----------- |
| `so_module_find_export(mod, name)` | Find exported symbol address |
| `so_module_find_import(mod, name)` | Find imported symbol address |

### Module hack

| Function | Description |
| -------- | ----------- |
| `so_module_hook_address(mod, vaddr, repl, ref)` | Hook by module-relative vaddr (pass `vaddr \| 1` for Thumb) |
| `so_module_hook_export(mod, name, repl, ref)` | Hook an exported function by symbol name |
| `so_module_hook_import(mod, name, repl, ref)` | Hook an imported function by symbol name |
| `so_module_inject_data(mod, vaddr, data, len)` | Patch data by module-relative vaddr |

### Module info

| Function | Description |
| -------- | ----------- |
| `so_module_get_name(mod)` | Get module soname |
| `so_module_get_entry_point(mod)` | Get module entry point |
| `so_module_get_load_base(mod)` | Get load base address |
| `so_module_get_load_size(mod)` | Get load size |
| `so_module_get_load_bias(mod)` | Get load bias |

## Virtual Libraries Included

- `libc.so` - C standard library
- `libstdc++.so` - C++ standard library
- `libm.so` - Math library
- `libGLESv1_CM.so` / `libGLESv2.so` / `libEGL.so` - OpenGL ES
- `libdl.so` - Dynamic loading library
- `liblog.so` - Android logging library
- ...

## Requirements

- [kubridge.skprx](https://github.com/bythos14/kubridge)
- [libshacccg.suprx](https://github.com/Rinnegatamante/ShaRKBR33D)

## Credits

- [TheFloW](https://github.com/TheOfficialFloW)
- [Rinnegatamante](https://github.com/Rinnegatamante)
- ...

## License

MIT — see [LICENSE](LICENSE)
