/*
 * Exact ARM base-PCS adapters for the ARMv5 game's software FP imports.
 *
 * Float arguments/results use r0/r1; doubles use r0:r1 and r2:r3.  Never
 * expose the VFP-register (hardfp) PCS to the Android SO.  These functions
 * deliberately do not replace the global libgcc symbols: exceptional cases
 * tail-call those original software helpers with the original arguments.
 *
 * libgcc's software arithmetic rounds to nearest/even independently of
 * FPSCR.  Use VFP only in that rounding mode, without enabled FP traps or
 * legacy vector modes.  FZ/DN may remain enabled: all FP inputs and outputs
 * on this path are normal finite numbers.  A flushed/underflowed result is
 * rejected too.  Save and restore the ENTIRE FPSCR, including cumulative
 * exceptions and comparison flags, so acceleration has no FP-state effects.
 * d0-d3 are caller-clobbered in both the base and VFP variants of AAPCS.
 */
.syntax unified
.arch armv7-a
.fpu vfpv3
.arm
.eabi_attribute 28, 0 /* Tag_ABI_VFP_args: base PCS */
.eabi_attribute 20, 1 /* denormals needed */
.eabi_attribute 21, 1 /* exceptions needed */
.eabi_attribute 23, 3 /* IEEE 754 */

.macro ENTRY name
    .section .text.el_\name,"ax",%progbits
    .align 2
    .global el_\name
    .type el_\name, %function
el_\name:
.endm
.macro END name
    .size el_\name, .-el_\name
.endm

/* Branch 9 is before any operation, 8 restores FPSCR before fallback. */
.macro FP_STATE nearest=1
    vmrs ip, fpscr
    .if \nearest
    tst ip, #0x00f70000 /* RMode, Stride, Len */
    .else
    tst ip, #0x00370000 /* conversion/comparison is rounding-independent */
    .endif
    bne 9f
    tst ip, #0x00009f00 /* exception enables including input-denormal trap */
    bne 9f
    vmov s6, ip
.endm
.macro FP_RESTORE
    vmov ip, s6
    vmsr fpscr, ip
.endm
.macro FALLBACK name
8:  FP_RESTORE
9:  b \name
    END \name
.endm

/* After removing the sign, biasing by one exponent step wraps Inf/NaN
 * below the lower bound, alongside zero and subnormal values. */
.macro NORMAL_F reg, target=9
    mov ip, \reg, lsl #1
    add ip, ip, #0x01000000
    cmp ip, #0x02000000
    blo \target\()f
.endm
.macro NORMAL_D high, target=9
    mov ip, \high, lsl #1
    add ip, ip, #0x00200000
    cmp ip, #0x00400000
    blo \target\()f
.endm

.macro FLOAT_BINARY name, op
    ENTRY \name
    FP_STATE
    NORMAL_F r0
    NORMAL_F r1
    vmov s0, r0
    vmov s1, r1
    \op\().f32 s2, s0, s1
    vmov ip, s2
    NORMAL_F ip, 8
    FP_RESTORE
    vmov r0, s2
    bx lr
    FALLBACK \name
.endm
.macro DOUBLE_BINARY name, op, near=0
    ENTRY \name
    FP_STATE
    NORMAL_D r1
    NORMAL_D r3
    vmov d0, r0, r1
    vmov d1, r2, r3
    .if \near
    /* ARM libgcc's signed alignment/sticky path at exponent gaps above
     * 31 has observable 1-ULP historical results. Preserve it rather than
     * silently changing the game's results to the VFP IEEE result. d0
     * already holds the original r0, so r0 can be restored before branching.
     */
    ubfx ip, r1, #20, #11
    ubfx r0, r3, #20, #11
    sub ip, ip, r0
    vmov r0, s0
    add ip, ip, #31
    cmp ip, #62
    bhi 9f
    .endif
    \op\().f64 d2, d0, d1
    vmov ip, s5
    NORMAL_D ip, 8
    FP_RESTORE
    vmov r0, r1, d2
    bx lr
    FALLBACK \name
.endm

FLOAT_BINARY __aeabi_fadd, vadd
FLOAT_BINARY __aeabi_fsub, vsub
FLOAT_BINARY __aeabi_fmul, vmul
FLOAT_BINARY __aeabi_fdiv, vdiv
DOUBLE_BINARY __aeabi_dadd, vadd, 1
DOUBLE_BINARY __aeabi_dsub, vsub, 1
DOUBLE_BINARY __aeabi_dmul, vmul
DOUBLE_BINARY __aeabi_ddiv, vdiv

.macro FLOAT_CMP name, cond
    ENTRY \name
    FP_STATE 0
    NORMAL_F r0
    NORMAL_F r1
    vmov s0, r0
    vmov s1, r1
    vcmp.f32 s0, s1
    vmrs APSR_nzcv, fpscr
    FP_RESTORE
    mov r0, #0
    mov\cond r0, #1
    bx lr
    FALLBACK \name
.endm
.macro DOUBLE_CMP name, cond
    ENTRY \name
    FP_STATE 0
    NORMAL_D r1
    NORMAL_D r3
    vmov d0, r0, r1
    vmov d1, r2, r3
    vcmp.f64 d0, d1
    vmrs APSR_nzcv, fpscr
    FP_RESTORE
    mov r0, #0
    mov\cond r0, #1
    bx lr
    FALLBACK \name
.endm
FLOAT_CMP __aeabi_fcmpeq, eq
FLOAT_CMP __aeabi_fcmplt, mi
FLOAT_CMP __aeabi_fcmple, ls
FLOAT_CMP __aeabi_fcmpge, ge
FLOAT_CMP __aeabi_fcmpgt, gt
DOUBLE_CMP __aeabi_dcmpeq, eq
DOUBLE_CMP __aeabi_dcmplt, mi
DOUBLE_CMP __aeabi_dcmple, ls
DOUBLE_CMP __aeabi_dcmpge, ge
DOUBLE_CMP __aeabi_dcmpgt, gt

ENTRY __aeabi_f2d
    FP_STATE 0
    NORMAL_F r0
    vmov s0, r0
    vcvt.f64.f32 d1, s0
    FP_RESTORE
    vmov r0, r1, d1
    bx lr
    FALLBACK __aeabi_f2d

ENTRY __aeabi_d2f
    FP_STATE
    NORMAL_D r1
    vmov d0, r0, r1
    vcvt.f32.f64 s2, d0
    vmov ip, s2
    NORMAL_F ip, 8
    FP_RESTORE
    vmov r0, s2
    bx lr
    FALLBACK __aeabi_d2f

.macro INT_FLOAT name, kind
    ENTRY \name
    FP_STATE
    vmov s0, r0
    vcvt.f32.\kind s1, s0
    FP_RESTORE
    vmov r0, s1
    bx lr
    FALLBACK \name
.endm
.macro INT_DOUBLE name, kind
    ENTRY \name
    FP_STATE 0
    vmov s0, r0
    vcvt.f64.\kind d1, s0
    FP_RESTORE
    vmov r0, r1, d1
    bx lr
    FALLBACK \name
.endm
INT_FLOAT __aeabi_i2f, s32
INT_FLOAT __aeabi_ui2f, u32
INT_DOUBLE __aeabi_i2d, s32
INT_DOUBLE __aeabi_ui2d, u32

/* VCVT (not VCVTR) truncates toward zero irrespective of FPSCR.RMode.
 * Out-of-range/NaN conversions must use libgcc's own saturation rules.
 * Subnormals truncate to zero even under FZ, so are safe here. */
ENTRY __aeabi_f2iz
    FP_STATE 0
    mov ip, r0, lsl #1
    cmp ip, #0x9e000000 /* |x| < 2^31 */
    bhs 9f
    vmov s0, r0
    vcvt.s32.f32 s1, s0
    FP_RESTORE
    vmov r0, s1
    bx lr
    FALLBACK __aeabi_f2iz

ENTRY __aeabi_d2iz
    FP_STATE 0
    ubfx ip, r1, #20, #11
    add ip, ip, #2
    cmp ip, #1056 /* exponent < 1023 + 31 */
    bhs 9f
    vmov d0, r0, r1
    vcvt.s32.f64 s2, d0
    FP_RESTORE
    vmov r0, s2
    bx lr
    FALLBACK __aeabi_d2iz

ENTRY __aeabi_d2uiz
    FP_STATE 0
    tst r1, #0x80000000
    bne 9f
    ubfx ip, r1, #20, #11
    add ip, ip, #1
    cmp ip, #1056 /* exponent < 1023 + 32 */
    bhs 9f
    vmov d0, r0, r1
    vcvt.u32.f64 s2, d0
    FP_RESTORE
    vmov r0, s2
    bx lr
    FALLBACK __aeabi_d2uiz

.section .note.GNU-stack,"",%progbits
