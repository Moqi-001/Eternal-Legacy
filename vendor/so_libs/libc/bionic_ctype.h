#ifndef BIONIC_CTYPE_H
#define BIONIC_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

#define CTYPE_NUM_CHARS 256

// bionic 导出的 _ctype_ / _tolower_tab_ / _toupper_tab_ 是"指向表的指针"，
// 不是表本身：调用方取到符号后还会解引用一次，再按 [c + 1] 取值。
extern const char  *BIONIC_ctype_;
extern const short *BIONIC_tolower_tab_;
extern const short *BIONIC_toupper_tab_;

#ifdef __cplusplus
};
#endif

#endif