#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <so_common/so_logger.h>
#include <so_linker/so_linker.h>

#include "dl.h"

/*
 * dlopen 的返回值必须长得像 bionic 的 soinfo。
 *
 * 安卓上 dlopen 本来就返回 soinfo*，原生库把它当 soinfo 读是常态：
 * StormGLOFT 的 hook_import_function 拿它查 strtab/symtab/hash 与
 * .rel.plt，找到目标符号的 GOT 槽再改写。句柄换成别的结构，它按
 * soinfo 的偏移会读到野指针（实机崩在它的符号名 strcmp 上）。
 *
 * 所以句柄结构体把 bionic 4.4（kitkat linker/link_image.h）的 soinfo
 * 整个内嵌在末尾，dlopen 返回 &info；mod/魔数等私有成员放在它前面——
 * soinfo 以后自己扩展、或者被消费者越界写，都伤不到私有成员。
 * 偏移与 kitkat 的排布一致，StormGLOFT 实读过的字段（name/base/
 * strtab/symtab/hash/plt_rel/plt_rel_count）已用 objdump 逐一核对；
 * 更深处（init/fini 数组之后）kitkat 还有链接器私有成员，分配时留了
 * 尾部余量兜住越界读写。
 */
typedef struct soinfo {
    char     name[0x80];      // +0x00
    uint32_t phdr;            // +0x80
    uint32_t phnum;           // +0x84
    uint32_t entry;           // +0x88
    uint32_t base;            // +0x8c 实址 = 虚址 + base
    uint32_t size;            // +0x90
    uint32_t unused;          // +0x94
    uint32_t dynamic;         // +0x98
    uint32_t unused2;         // +0x9c
    uint32_t unused3;         // +0xa0
    uint32_t next;            // +0xa4
    uint32_t flags;           // +0xa8
    uint32_t strtab;          // +0xac
    uint32_t symtab;          // +0xb0
    uint32_t nbucket;         // +0xb4
    uint32_t nchain;          // +0xb8
    uint32_t bucket;          // +0xbc
    uint32_t chain;           // +0xc0
    uint32_t plt_got;         // +0xc4（我们的重定位不走 GOT 桩，恒 0）
    uint32_t plt_rel;         // +0xc8
    uint32_t plt_rel_count;   // +0xcc
    uint32_t plt_rela;        // +0xd0（RELA 模块报空，消费者按 count=0 跳过）
    uint32_t plt_rela_count;  // +0xd4
    uint32_t rel;             // +0xd8
    uint32_t rel_count;       // +0xdc
    uint32_t rela;            // +0xe0
    uint32_t rela_count;      // +0xe4
    uint32_t preinit_array;   // +0xe8
    uint32_t preinit_count;   // +0xec
    uint32_t init_array;      // +0xf0
    uint32_t init_count;      // +0xf4
    uint32_t fini_array;      // +0xf8
    uint32_t fini_count;      // +0xfc
    uint32_t init_func;       // +0x100
    uint32_t fini_func;       // +0x104
} soinfo;

/* info 之后的分配余量：kitkat 在 fini_func 之后还有链接器私有成员，
   按 soinfo 深入的越界读写先落进这段缓冲，砸不到堆元数据 */
#define SOINFO_TAIL_SLACK 64

typedef struct dl_handle {
    uint32_t   magic;  // 私有区：放在前面，info 之后怎么越界都伤不到它
    so_module *mod;
    soinfo     info;  // 放最后：dlopen 返回 &info，soinfo 自身扩展或被越界写都不影响私有成员
} dl_handle;

// info 必须是最后一个成员
_Static_assert(offsetof(dl_handle, info) + sizeof(soinfo) == sizeof(dl_handle),
               "info 必须是最后一个成员");

#define SOINFO_MAGIC   0x444C4F53  // "SOLD"
#define MAX_DL_HANDLES 8

// 成员反推容器（参考 JavaObject 的 headerFromObject）
static inline dl_handle *dl_handle_from_info(void *info) {
    return (dl_handle *)((char *)info - offsetof(dl_handle, info));
}

static dl_handle *s_handles[MAX_DL_HANDLES];
static uint32_t   s_empty_bucket;
static uint32_t   s_empty_chain;

static dl_handle *dl_handle_find(so_module *mod) {
    for (int i = 0; i < MAX_DL_HANDLES; i++) {
        if (s_handles[i] && s_handles[i]->mod == mod) {
            return s_handles[i];
        }
    }

    return NULL;
}

static dl_handle *dl_handle_create(so_module *mod) {
    so_elfinfo elfinfo;
    if (so_module_get_elfinfo(mod, &elfinfo) < 0) {
        return NULL;
    }

    dl_handle *handle = calloc(1, sizeof(dl_handle) + SOINFO_TAIL_SLACK);
    if (!handle) {
        return NULL;
    }

    const char *soname = elfinfo.soname ? elfinfo.soname : "";
    strncpy(handle->info.name, soname, sizeof(handle->info.name) - 1);

    handle->info.base   = (uint32_t)elfinfo.load_bias;
    handle->info.size   = (uint32_t)elfinfo.load_size;
    handle->info.strtab = (uint32_t)(uintptr_t)elfinfo.dynstr;
    handle->info.symtab = (uint32_t)(uintptr_t)elfinfo.dynsym;

    if (elfinfo.hash) {
        uint32_t nbucket = elfinfo.hash[0];

        handle->info.nbucket = nbucket;
        handle->info.nchain  = elfinfo.hash[1];
        handle->info.bucket  = (uint32_t)(uintptr_t)(elfinfo.hash + 2);
        handle->info.chain   = (uint32_t)(uintptr_t)(elfinfo.hash + 2 + nbucket);
    } else {
        // 只有 GNU hash 的模块给空表：查不到符号就跳过，不会踩野指针
        handle->info.nbucket = 1;
        handle->info.nchain  = 0;
        handle->info.bucket  = (uint32_t)(uintptr_t)&s_empty_bucket;
        handle->info.chain   = (uint32_t)(uintptr_t)&s_empty_chain;
    }

    // 引擎按 Elf32_Rel（8 字节）遍历，RELA 格式的模块只能报空
    if (elfinfo.rel_plt) {
        handle->info.plt_rel       = (uint32_t)(uintptr_t)elfinfo.rel_plt;
        handle->info.plt_rel_count = (uint32_t)elfinfo.num_rel_plt;
    }

    handle->magic = SOINFO_MAGIC;
    handle->mod   = mod;

    return handle;
}

static dl_handle *dl_handle_get(so_module *mod) {
    dl_handle *handle = dl_handle_find(mod);
    if (handle) {
        return handle;
    }

    handle = dl_handle_create(mod);
    if (!handle) {
        return NULL;
    }

    for (int i = 0; i < MAX_DL_HANDLES; i++) {
        if (!s_handles[i]) {
            s_handles[i] = handle;
            return handle;
        }
    }

    // 槽位不够就当没打开（项目里只有两个游戏库会走 dlopen）
    so_loge("[libdl] too many dlopen handles!");
    free(handle);
    return NULL;
}

// 句柄是我们发的 &info：先由成员反推容器，再在表里核对；
// 外来指针只参与指针运算不提前解引用，魔数做命中后的二次确认
static dl_handle *dl_handle_cast(void *handle) {
    dl_handle *h = dl_handle_from_info(handle);

    for (int i = 0; i < MAX_DL_HANDLES; i++) {
        if (s_handles[i] == h && h->magic == SOINFO_MAGIC) {
            return h;
        }
    }

    return NULL;
}

void *dlopen_soloader(const char *filename, int flag) {
    if (!filename) {
        return NULL;
    }

    const char *soname = strrchr(filename, '/');
    if (!soname++) {
        soname = filename;
    }

    so_module *mod = so_linker_find_module(soname);
    if (!mod) {
        so_logw("[libdl] dlopen(%s,0x%x): not found!", filename, flag);
        return NULL;
    }

    dl_handle *handle = dl_handle_get(mod);
    if (!handle) {
        so_loge("[libdl] dlopen(%s,0x%x): cannot create handle!", filename, flag);
        return NULL;
    }

    so_logd("[libdl] dlopen(%s,0x%x): %p ('%s')", filename, flag, &handle->info, soname);
    return &handle->info;
}

void *dlsym_soloader(void *handle, const char *symbol) {
    if (!handle || !symbol) {
        return NULL;
    }

    dl_handle *h = dl_handle_cast(handle);
    if (!h) {
        so_loge("[libdl] dlsym(%p,%s): invalid handle!", handle, symbol);
        return NULL;
    }

    void *sym = so_module_find_export(h->mod, symbol);
    if (!sym) {
        const char *soname = so_module_get_name(h->mod);
        so_logw("[libdl] %s: '%s' not found!", soname ? soname : "?", symbol);
        return NULL;
    }

    so_logd("[libdl] dlsym(%p,%s): %p", handle, symbol, sym);
    return sym;
}

void dlclose_soloader(void *handle) {
    so_logd("[libdl] dlclose(%p): ignored", handle);
}

char *dlerror_soloader(void) {
    so_logd("[libdl] dlerror(): ignored");
    return NULL;
}
