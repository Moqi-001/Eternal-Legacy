#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <so_common/so_logger.h>
#include <so_linker/so_linker.h>

#include "dl.h"

/*
 * dlopen 的返回值必须长得像 bionic 的 soinfo。
 *
 * 安卓上的原生库会直接把 dlopen 的句柄当 soinfo* 用：StormGLOFT 的
 * hook_import_function 就是拿它去查 strtab/symtab/hash 与 .rel.plt，
 * 找到目标符号的 GOT 槽再改写。句柄换成别的结构，它会按 soinfo 的偏移
 * 读到野指针（实机崩在它的符号名 strcmp 上）。
 *
 * 这里只摆它真正读到的字段，偏移与 bionic 4.x 的 soinfo 一致：
 *   +0x00 name[128]  +0x8c base    +0xac strtab  +0xb0 symtab
 *   +0xb4 nbucket    +0xb8 nchain  +0xbc bucket  +0xc0 chain
 *   +0xc8 plt_rel    +0xcc plt_rel_count
 */
#define SOINFO_MAGIC 0x444C4F53  // "SOLD"

typedef struct so_dl_handle {
    char     name[0x80];
    uint32_t phdr;           // 0x80（引擎不读，留空）
    uint32_t phnum;          // 0x84
    uint32_t entry;          // 0x88
    uint32_t base;           // 0x8c 实址 = 虚址 + base
    uint32_t size;           // 0x90
    uint32_t unused1;        // 0x94
    uint32_t dynamic;        // 0x98
    uint32_t unused2;        // 0x9c
    uint32_t unused3;        // 0xa0
    uint32_t next;           // 0xa4
    uint32_t flags;          // 0xa8
    uint32_t strtab;         // 0xac
    uint32_t symtab;         // 0xb0
    uint32_t nbucket;        // 0xb4
    uint32_t nchain;         // 0xb8
    uint32_t bucket;         // 0xbc
    uint32_t chain;          // 0xc0
    uint32_t unused4;        // 0xc4
    uint32_t plt_rel;        // 0xc8
    uint32_t plt_rel_count;  // 0xcc

    // 以下只给我们自己用，引擎不会读
    uint32_t   magic;
    so_module *mod;
} so_dl_handle;

#define MAX_DL_HANDLES 8

static so_dl_handle *s_handles[MAX_DL_HANDLES];
static uint32_t      s_empty_bucket;
static uint32_t      s_empty_chain;

static so_dl_handle *so_dl_handle_find(so_module *mod) {
    for (int i = 0; i < MAX_DL_HANDLES; i++) {
        if (s_handles[i] && s_handles[i]->mod == mod) {
            return s_handles[i];
        }
    }

    return NULL;
}

static so_dl_handle *so_dl_handle_create(so_module *mod) {
    so_elf_tables tables;
    if (so_module_get_elf_tables(mod, &tables) < 0) {
        return NULL;
    }

    so_dl_handle *handle = calloc(1, sizeof(so_dl_handle));
    if (!handle) {
        return NULL;
    }

    const char *soname = tables.soname ? tables.soname : "";
    strncpy(handle->name, soname, sizeof(handle->name) - 1);

    handle->base   = (uint32_t)tables.load_bias;
    handle->strtab = (uint32_t)(uintptr_t)tables.dynstr;
    handle->symtab = (uint32_t)(uintptr_t)tables.dynsym;

    if (tables.hash) {
        uint32_t nbucket = tables.hash[0];

        handle->nbucket = nbucket;
        handle->nchain  = tables.hash[1];
        handle->bucket  = (uint32_t)(uintptr_t)(tables.hash + 2);
        handle->chain   = (uint32_t)(uintptr_t)(tables.hash + 2 + nbucket);
    } else {
        // 只有 GNU hash 的模块给空表：查不到符号就跳过，不会踩野指针
        handle->nbucket = 1;
        handle->nchain  = 0;
        handle->bucket  = (uint32_t)(uintptr_t)&s_empty_bucket;
        handle->chain   = (uint32_t)(uintptr_t)&s_empty_chain;
    }

    // 引擎按 Elf32_Rel（8 字节）遍历，RELA 格式的模块只能报空
    if (tables.rel_plt) {
        handle->plt_rel       = (uint32_t)(uintptr_t)tables.rel_plt;
        handle->plt_rel_count = (uint32_t)tables.num_rel_plt;
    }

    handle->magic = SOINFO_MAGIC;
    handle->mod   = mod;

    return handle;
}

static so_dl_handle *so_dl_handle_get(so_module *mod) {
    so_dl_handle *handle = so_dl_handle_find(mod);
    if (handle) {
        return handle;
    }

    handle = so_dl_handle_create(mod);
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

static so_dl_handle *so_dl_handle_cast(void *handle) {
    so_dl_handle *h = (so_dl_handle *)handle;
    if (!h || h->magic != SOINFO_MAGIC) {
        return NULL;
    }

    return h;
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

    so_dl_handle *handle = so_dl_handle_get(mod);
    if (!handle) {
        so_loge("[libdl] dlopen(%s,0x%x): cannot create handle!", filename, flag);
        return NULL;
    }

    so_logd("[libdl] dlopen(%s,0x%x): %p ('%s')", filename, flag, handle, soname);
    return handle;
}

void *dlsym_soloader(void *handle, const char *symbol) {
    if (!handle || !symbol) {
        return NULL;
    }

    so_dl_handle *h = so_dl_handle_cast(handle);
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
