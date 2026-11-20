/*
 * ARM ABI 辅助函数（编译器为浮点比较生成的调用）。
 *
 * VitaSDK 的 libgcc 里没有 __aeabi_dcmpun，而编译器在判断"任一操作数是 NaN"
 * 时会生成对它的调用。按 AEABI 语义：无序（任意一边是 NaN）返回非 0。
 *
 * 用位模式判断，不走 a != a —— 本工程带 -ffast-math，浮点自比较会被优化掉。
 */
#include <stdint.h>

int __aeabi_dcmpun(double a, double b) {
    union {
        double          d;
        unsigned long long u;
    } va = { a }, vb = { b };

    const unsigned long long exp  = 0x7FF0000000000000ull;
    const unsigned long long frac = 0x000FFFFFFFFFFFFFull;

    int a_nan = (va.u & exp) == exp && (va.u & frac) != 0;
    int b_nan = (vb.u & exp) == exp && (vb.u & frac) != 0;

    return a_nan || b_nan;
}
