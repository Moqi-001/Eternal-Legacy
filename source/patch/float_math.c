#include "float_math.h"

#include <so_common/so_logger.h>

/* Assembly-only implementations avoid both -ffast-math transformations and
 * accidental hardfp calls.  Addresses, not C floating expressions, are used.
 * Keep libgcc's global symbols intact: they are the adapters' fallbacks.
 * The production softfp newlib libm already uses VFP; retain its full-domain
 * behavior, double precision, errno, NaNs and signed zero.
 * VFPv3 has no direct 64-bit integer conversion, so
 * __aeabi_d2lz / __aeabi_ul2d retain libgcc, as does unordered __aeabi_dcmpun.
 */
#define FP_IMPORTS(X) \
    X(__aeabi_fadd) X(__aeabi_fsub) X(__aeabi_fmul) X(__aeabi_fdiv) \
    X(__aeabi_dadd) X(__aeabi_dsub) X(__aeabi_dmul) X(__aeabi_ddiv) \
    X(__aeabi_fcmpeq) X(__aeabi_fcmplt) X(__aeabi_fcmple) \
    X(__aeabi_fcmpge) X(__aeabi_fcmpgt) \
    X(__aeabi_dcmpeq) X(__aeabi_dcmplt) X(__aeabi_dcmple) \
    X(__aeabi_dcmpge) X(__aeabi_dcmpgt) \
    X(__aeabi_f2d) X(__aeabi_d2f) \
    X(__aeabi_i2f) X(__aeabi_ui2f) X(__aeabi_i2d) X(__aeabi_ui2d) \
    X(__aeabi_f2iz) X(__aeabi_d2iz) X(__aeabi_d2uiz)

#define DECLARE_ADAPTER(name) extern void el_##name(void);
FP_IMPORTS(DECLARE_ADAPTER)
#undef DECLARE_ADAPTER

int soloader_patch_float_math(so_module *mod) {
    if (!mod) return -1;
    const struct {
        const char *name;
        void (*adapter)(void);
    } imports[] = {
#define IMPORT_ADAPTER(name) { #name, el_##name },
        FP_IMPORTS(IMPORT_ADAPTER)
#undef IMPORT_ADAPTER
    };
    unsigned installed = 0;
    for (unsigned i = 0; i < sizeof(imports) / sizeof(imports[0]); ++i) {
        if (!so_module_find_import(mod, imports[i].name)) continue;
        if (so_module_hook_import(mod, imports[i].name, imports[i].adapter, NULL) < 0)
            return -1;
        ++installed;
    }
    so_loga("[FLOAT_MATH] exact_vfp=%u base_pcs=1 fpscr_preserved=1 "
            "exceptional=libgcc math=softfp_newlib int64=libgcc", installed);
    return 0;
}
