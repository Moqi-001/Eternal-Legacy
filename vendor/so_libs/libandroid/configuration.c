#include <stdlib.h>
#include <string.h>

#include <psp2/apputil.h>
#include <psp2/system_param.h>

#include <so_common/so_logger.h>

#include "configuration.h"

#define VITA_SCREEN_WIDTH  960
#define VITA_SCREEN_HEIGHT 544
#define VITA_DENSITY_DPI   220  // 约 220 DPI
#define VITA_SDK_VERSION   19   // Android 4.4

struct AConfiguration {
    uint16_t mcc;
    uint16_t mnc;
    char     language[2];
    char     country[2];
    uint8_t  orientation;
    uint8_t  touchscreen;
    uint16_t density;
    uint8_t  keyboard;
    uint8_t  navigation;
    uint8_t  keysHidden;
    uint8_t  navHidden;
    uint16_t sdkVersion;
    uint16_t minorVersion;
    uint16_t screenWidth;
    uint16_t screenHeight;
    uint8_t  screenSize;
    uint8_t  screenLong;
    uint8_t  uiModeType;
    uint8_t  uiModeNight;
    uint16_t screenWidthDp;
    uint16_t screenHeightDp;
    uint16_t smallestScreenWidthDp;
    uint8_t  layoutDirection;
};

/*
 * 系统语言映射成 Android 的两位语言/地区码（ISO 639-1 / ISO 3166）。
 * 引擎拿 AConfiguration_getLanguage 挑语言资源，中文必须回 "zh"。
 */
static void system_locale(char *lang, char *country) {
    int v = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &v) < 0) {
        v = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    }

    const char *l = "en";
    const char *c = "US";
    switch (v) {
        case SCE_SYSTEM_PARAM_LANG_JAPANESE:       l = "ja"; c = "JP"; break;
        case SCE_SYSTEM_PARAM_LANG_FRENCH:         l = "fr"; c = "FR"; break;
        case SCE_SYSTEM_PARAM_LANG_SPANISH:        l = "es"; c = "ES"; break;
        case SCE_SYSTEM_PARAM_LANG_GERMAN:         l = "de"; c = "DE"; break;
        case SCE_SYSTEM_PARAM_LANG_ITALIAN:        l = "it"; c = "IT"; break;
        case SCE_SYSTEM_PARAM_LANG_DUTCH:          l = "nl"; c = "NL"; break;
        case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT:  l = "pt"; c = "PT"; break;
        case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR:  l = "pt"; c = "BR"; break;
        case SCE_SYSTEM_PARAM_LANG_RUSSIAN:        l = "ru"; c = "RU"; break;
        case SCE_SYSTEM_PARAM_LANG_KOREAN:         l = "ko"; c = "KR"; break;
        case SCE_SYSTEM_PARAM_LANG_CHINESE_T:      l = "zh"; c = "TW"; break;
        case SCE_SYSTEM_PARAM_LANG_CHINESE_S:      l = "zh"; c = "CN"; break;
        default: break;
    }
    memcpy(lang, l, 2);
    memcpy(country, c, 2);
}

// config 里没设过语言（language[0] 为 0）就用系统语言补齐，并缓存回 config
static void ensure_locale(AConfiguration *config) {
    if (config->language[0] != '\0' && config->country[0] != '\0') {
        return;
    }
    char lang[2];
    char country[2];
    system_locale(lang, country);
    if (config->language[0] == '\0') {
        memcpy(config->language, lang, 2);
    }
    if (config->country[0] == '\0') {
        memcpy(config->country, country, 2);
    }
}

AConfiguration *AConfiguration_new() {
    AConfiguration *config = malloc(sizeof(AConfiguration));
    memset(config, 0, sizeof(AConfiguration));
    so_logd("AConfiguration_new(): %p", config);
    return config;
}

void AConfiguration_delete(AConfiguration *config) {
    so_logd("AConfiguration_delete(%p)", config);
    free(config);
}

void AConfiguration_fromAssetManager(AConfiguration *out, AAssetManager *am) {
    out->mcc = 0;  // 移动国家码
    out->mnc = 0;  // 移动网络码

    // 语言/地区不在这里写死：留 0，由 AConfiguration_getLanguage/getCountry
    // 回落系统语言，引擎自己 setLanguage 过的优先
    out->language[0] = 0;
    out->language[1] = 0;
    out->country[0]  = 0;
    out->country[1]  = 0;

    out->orientation  = ACONFIGURATION_ORIENTATION_LAND;    // 屏幕方向 (横屏)
    out->screenSize   = ACONFIGURATION_SCREENSIZE_NORMAL;   // 屏幕尺寸 (标准: 4-6英寸)
    out->screenLong   = ACONFIGURATION_SCREENLONG_YES;      // 是否长屏 (是)
    out->density      = VITA_DENSITY_DPI;                   // 像素密度 (220 dpi)
    out->touchscreen  = ACONFIGURATION_TOUCHSCREEN_FINGER;  // 触屏类型 (手指)
    out->keyboard     = ACONFIGURATION_KEYBOARD_NOKEYS;     // 物理键盘 (无)
    out->navigation   = ACONFIGURATION_NAVIGATION_DPAD;     // 非触摸导航方式 (方向键)
    out->keysHidden   = ACONFIGURATION_KEYSHIDDEN_YES;      // 键盘是否隐藏 (是)
    out->navHidden    = ACONFIGURATION_NAVHIDDEN_NO;        // 导航设备是否隐藏 (否)
    out->sdkVersion   = VITA_SDK_VERSION;                   // Android 4.4 API 19
    out->minorVersion = 0;
    out->uiModeType   = ACONFIGURATION_UI_MODE_TYPE_NORMAL;  // UI 类型 (标准应用)
    out->uiModeNight  = ACONFIGURATION_UI_MODE_NIGHT_NO;     // 夜间模式 (否)

    // DP = 像素 / (DPI / 160)
    out->screenWidthDp         = (uint16_t)(VITA_SCREEN_WIDTH * 160 / VITA_DENSITY_DPI);   // 屏幕宽度DP ≈ 698
    out->screenHeightDp        = (uint16_t)(VITA_SCREEN_HEIGHT * 160 / VITA_DENSITY_DPI);  // 屏幕高度DP ≈ 395
    out->smallestScreenWidthDp = (uint16_t)(VITA_SCREEN_HEIGHT * 160 / VITA_DENSITY_DPI);  // 最小屏幕宽度DP ≈ 395

    out->layoutDirection = ACONFIGURATION_LAYOUTDIR_LTR;  // 布局方向 (从左到右)

    so_logd("AConfiguration_fromAssetManager(%p,%p)", out, am);
}

void AConfiguration_copy(AConfiguration *dest, AConfiguration *src) {
    *dest = *src;
    so_logd("AConfiguration_copy(%p,%p)", dest, src);
}

int32_t AConfiguration_getMcc(AConfiguration *config) {
    so_logd("AConfiguration_getMcc(%p): %d", config, config->mcc);
    return config->mcc;
}

int32_t AConfiguration_getMnc(AConfiguration *config) {
    so_logd("AConfiguration_getMnc(%p): %d", config, config->mnc);
    return config->mnc;
}

void AConfiguration_getLanguage(AConfiguration *config, char *outLanguage) {
    ensure_locale(config);
    outLanguage[0] = config->language[0];
    outLanguage[1] = config->language[1];
    so_logd("AConfiguration_getLanguage(%p): %c%c", config, outLanguage[0], outLanguage[1]);
}

void AConfiguration_getCountry(AConfiguration *config, char *outCountry) {
    ensure_locale(config);
    outCountry[0] = config->country[0];
    outCountry[1] = config->country[1];
    so_logd("AConfiguration_getCountry(%p): %c%c", config, outCountry[0], outCountry[1]);
}

int32_t AConfiguration_getOrientation(AConfiguration *config) {
    so_logd("AConfiguration_getOrientation(%p): %d", config, config->orientation);
    return config->orientation;
}

int32_t AConfiguration_getTouchscreen(AConfiguration *config) {
    so_logd("AConfiguration_getTouchscreen(%p): %d", config, config->touchscreen);
    return config->touchscreen;
}

int32_t AConfiguration_getDensity(AConfiguration *config) {
    so_logd("AConfiguration_getDensity(%p): %d", config, config->density);
    return config->density;
}

int32_t AConfiguration_getKeyboard(AConfiguration *config) {
    so_logd("AConfiguration_getKeyboard(%p): %d", config, config->keyboard);
    return config->keyboard;
}

int32_t AConfiguration_getNavigation(AConfiguration *config) {
    so_logd("AConfiguration_getNavigation(%p): %d", config, config->navigation);
    return config->navigation;
}

int32_t AConfiguration_getKeysHidden(AConfiguration *config) {
    so_logd("AConfiguration_getKeysHidden(%p): %d", config, config->keysHidden);
    return config->keysHidden;
}

int32_t AConfiguration_getNavHidden(AConfiguration *config) {
    so_logd("AConfiguration_getNavHidden(%p): %d", config, config->navHidden);
    return config->navHidden;
}

int32_t AConfiguration_getSdkVersion(AConfiguration *config) {
    so_logd("AConfiguration_getSdkVersion(%p): %d", config, config->sdkVersion);
    return config->sdkVersion;
}

int32_t AConfiguration_getScreenSize(AConfiguration *config) {
    so_logd("AConfiguration_getScreenSize(%p): %d", config, config->screenSize);
    return config->screenSize;
}

int32_t AConfiguration_getScreenLong(AConfiguration *config) {
    so_logd("AConfiguration_getScreenLong(%p): %d", config, config->screenLong);
    return config->screenLong;
}

int32_t AConfiguration_getUiModeType(AConfiguration *config) {
    so_logd("AConfiguration_getUiModeType(%p): %d", config, config->uiModeType);
    return config->uiModeType;
}

int32_t AConfiguration_getUiModeNight(AConfiguration *config) {
    so_logd("AConfiguration_getUiModeNight(%p): %d", config, config->uiModeNight);
    return config->uiModeNight;
}

int32_t AConfiguration_getScreenWidthDp(AConfiguration *config) {
    so_logd("AConfiguration_getScreenWidthDp(%p): %d", config, config->screenWidthDp);
    return config->screenWidthDp;
}

int32_t AConfiguration_getScreenHeightDp(AConfiguration *config) {
    so_logd("AConfiguration_getScreenHeightDp(%p): %d", config, config->screenHeightDp);
    return config->screenHeightDp;
}

int32_t AConfiguration_getSmallestScreenWidthDp(AConfiguration *config) {
    so_logd("AConfiguration_getSmallestScreenWidthDp(%p): %d", config, config->smallestScreenWidthDp);
    return config->smallestScreenWidthDp;
}

int32_t AConfiguration_getLayoutDirection(AConfiguration *config) {
    so_logd("AConfiguration_getLayoutDirection(%p): %d", config, config->layoutDirection);
    return config->layoutDirection;
}

// ----------------------------------------------------------------------

void AConfiguration_setMcc(AConfiguration *config, int32_t mcc) {
    config->mcc = mcc;
    so_logd("AConfiguration_setMcc(%p,%d)", config, mcc);
}

void AConfiguration_setMnc(AConfiguration *config, int32_t mnc) {
    config->mnc = mnc;
    so_logd("AConfiguration_setMnc(%p,%d)", config, mnc);
}

void AConfiguration_setLanguage(AConfiguration *config, const char *language) {
    config->language[0] = language[0];
    config->language[1] = language[1];
    so_logd("AConfiguration_setLanguage(%p,%c%c)", config, language[0], language[1]);
}

void AConfiguration_setCountry(AConfiguration *config, const char *country) {
    config->country[0] = country[0];
    config->country[1] = country[1];
    so_logd("AConfiguration_setCountry(%p,%c%c)", config, country[0], country[1]);
}

void AConfiguration_setOrientation(AConfiguration *config, int32_t orientation) {
    config->orientation = orientation;
    so_logd("AConfiguration_setOrientation(%p,%d)", config, orientation);
}

void AConfiguration_setTouchscreen(AConfiguration *config, int32_t touchscreen) {
    config->touchscreen = touchscreen;
    so_logd("AConfiguration_setTouchscreen(%p,%d)", config, touchscreen);
}

void AConfiguration_setDensity(AConfiguration *config, int32_t density) {
    config->density = density;
    so_logd("AConfiguration_setDensity(%p,%d)", config, density);
}

void AConfiguration_setKeyboard(AConfiguration *config, int32_t keyboard) {
    so_logd("AConfiguration_setKeyboard(%p,%d)", config, keyboard);
    config->keyboard = keyboard;
}

void AConfiguration_setNavigation(AConfiguration *config, int32_t navigation) {
    config->navigation = navigation;
    so_logd("AConfiguration_setNavigation(%p,%d)", config, navigation);
}

void AConfiguration_setKeysHidden(AConfiguration *config, int32_t keysHidden) {
    config->keysHidden = keysHidden;
    so_logd("AConfiguration_setKeysHidden(%p,%d)", config, keysHidden);
}

void AConfiguration_setNavHidden(AConfiguration *config, int32_t navHidden) {
    config->navHidden = navHidden;
    so_logd("AConfiguration_setNavHidden(%p,%d)", config, navHidden);
}

void AConfiguration_setSdkVersion(AConfiguration *config, int32_t sdkVersion) {
    config->sdkVersion = sdkVersion;
    so_logd("AConfiguration_setSdkVersion(%p,%d)", config, sdkVersion);
}

void AConfiguration_setScreenSize(AConfiguration *config, int32_t screenSize) {
    config->screenSize = screenSize;
    so_logd("AConfiguration_setScreenSize(%p,%d)", config, screenSize);
}

void AConfiguration_setScreenLong(AConfiguration *config, int32_t screenLong) {
    config->screenLong = screenLong;
    so_logd("AConfiguration_setScreenLong(%p,%d)", config, screenLong);
}

void AConfiguration_setUiModeType(AConfiguration *config, int32_t uiModeType) {
    config->uiModeType = uiModeType;
    so_logd("AConfiguration_setUiModeType(%p,%d)", config, uiModeType);
}

void AConfiguration_setUiModeNight(AConfiguration *config, int32_t uiModeNight) {
    config->uiModeNight = uiModeNight;
    so_logd("AConfiguration_setUiModeNight(%p,%d)", config, uiModeNight);
}

void AConfiguration_setScreenWidthDp(AConfiguration *config, int32_t value) {
    config->screenWidthDp = value;
    so_logd("AConfiguration_setScreenWidthDp(%p,%d)", config, value);
}

void AConfiguration_setScreenHeightDp(AConfiguration *config, int32_t value) {
    config->screenHeightDp = value;
    so_logd("AConfiguration_setScreenHeightDp(%p,%d)", config, value);
}

void AConfiguration_setSmallestScreenWidthDp(AConfiguration *config, int32_t value) {
    config->smallestScreenWidthDp = value;
    so_logd("AConfiguration_setSmallestScreenWidthDp(%p,%d)", config, value);
}

void AConfiguration_setLayoutDirection(AConfiguration *config, int32_t value) {
    config->layoutDirection = value;
    so_logd("AConfiguration_setLayoutDirection(%p,%d)", config, value);
}

// ----------------------------------------------------------------------

// ----------------------------------------------------------------------
// 资源选择三件套：diff / match / isBetterThan。
//
// 语义照 Android 的 ResTable_config，但比较的是我们自己的字段：AConfiguration 在
// NDK 里是不透明类型，App 只拿指针，所以不需要跟 androidfw 的内存布局一致。
// 用途是「同一份资源有多套目录配置时，挑一套当前环境下可用且最贴合的」。

/*
 * 两个配置哪些维度不同。返回 ACONFIGURATION_* 位掩码，每位置 1 表示该维度的值不同。
 */
int32_t AConfiguration_diff(AConfiguration *config1, AConfiguration *config2) {
    if (!config1 || !config2) {
        return 0;
    }

    int32_t diffs = 0;
    if (config1->mcc != config2->mcc) {
        diffs |= ACONFIGURATION_MCC;
    }
    if (config1->mnc != config2->mnc) {
        diffs |= ACONFIGURATION_MNC;
    }
    if (config1->language[0] != config2->language[0] ||
        config1->language[1] != config2->language[1] ||
        config1->country[0]  != config2->country[0] ||
        config1->country[1]  != config2->country[1]) {
        diffs |= ACONFIGURATION_LOCALE;
    }
    if (config1->layoutDirection != config2->layoutDirection) {
        diffs |= ACONFIGURATION_LAYOUTDIR;
    }
    if (config1->smallestScreenWidthDp != config2->smallestScreenWidthDp) {
        diffs |= ACONFIGURATION_SMALLEST_SCREEN_SIZE;
    }
    if (config1->screenWidthDp != config2->screenWidthDp ||
        config1->screenHeightDp != config2->screenHeightDp ||
        config1->screenWidth != config2->screenWidth ||
        config1->screenHeight != config2->screenHeight ||
        config1->screenSize != config2->screenSize ||
        config1->screenLong != config2->screenLong) {
        diffs |= ACONFIGURATION_SCREEN_SIZE;
    }
    if (config1->orientation != config2->orientation) {
        diffs |= ACONFIGURATION_ORIENTATION;
    }
    if (config1->uiModeType != config2->uiModeType ||
        config1->uiModeNight != config2->uiModeNight) {
        diffs |= ACONFIGURATION_UI_MODE;
    }
    if (config1->density != config2->density) {
        diffs |= ACONFIGURATION_DENSITY;
    }
    if (config1->touchscreen != config2->touchscreen) {
        diffs |= ACONFIGURATION_TOUCHSCREEN;
    }
    if (config1->keyboard != config2->keyboard) {
        diffs |= ACONFIGURATION_KEYBOARD;
    }
    if (config1->keysHidden != config2->keysHidden ||
        config1->navHidden != config2->navHidden) {
        diffs |= ACONFIGURATION_KEYBOARD_HIDDEN;
    }
    if (config1->navigation != config2->navigation) {
        diffs |= ACONFIGURATION_NAVIGATION;
    }
    if (config1->sdkVersion != config2->sdkVersion ||
        config1->minorVersion != config2->minorVersion) {
        diffs |= ACONFIGURATION_VERSION;
    }

    so_logd("AConfiguration_diff(%p,%p): 0x%04x", config1, config2, (unsigned)diffs);
    return diffs;
}

/*
 * base 这套配置在 requested 环境下能不能用：base 里任一「已指定」的维度跟 requested
 * 冲突就返回 0；base 没指定的维度视为通配，不参与比较。
 */
int32_t AConfiguration_match(AConfiguration *base, AConfiguration *requested) {
    if (!base || !requested) {
        return 0;
    }

    int32_t result = 1;
    if ((base->mcc != 0 && base->mcc != requested->mcc) ||
        (base->mnc != 0 && base->mnc != requested->mnc) ||
        (base->language[0] != 0 &&
         (base->language[0] != requested->language[0] || base->language[1] != requested->language[1])) ||
        (base->country[0] != 0 &&
         (base->country[0] != requested->country[0] || base->country[1] != requested->country[1])) ||
        (base->layoutDirection != 0 && base->layoutDirection != requested->layoutDirection) ||
        // 尺寸类只能「不超过」requested，超过即不可用
        (base->smallestScreenWidthDp != 0 && base->smallestScreenWidthDp > requested->smallestScreenWidthDp) ||
        (base->screenWidthDp != 0 && base->screenWidthDp > requested->screenWidthDp) ||
        (base->screenHeightDp != 0 && base->screenHeightDp > requested->screenHeightDp) ||
        (base->screenWidth != 0 && base->screenWidth > requested->screenWidth) ||
        (base->screenHeight != 0 && base->screenHeight > requested->screenHeight) ||
        (base->screenSize != 0 && base->screenSize > requested->screenSize) ||
        (base->screenLong != 0 && base->screenLong != requested->screenLong) ||
        (base->orientation != 0 && base->orientation != requested->orientation) ||
        (base->uiModeType != 0 && base->uiModeType != requested->uiModeType) ||
        (base->uiModeNight != 0 && base->uiModeNight != requested->uiModeNight) ||
        (base->density != 0 && base->density != requested->density) ||
        (base->touchscreen != 0 && base->touchscreen != requested->touchscreen) ||
        (base->keyboard != 0 && base->keyboard != requested->keyboard) ||
        (base->keysHidden != 0 && base->keysHidden != requested->keysHidden) ||
        (base->navHidden != 0 && base->navHidden != requested->navHidden) ||
        (base->navigation != 0 && base->navigation != requested->navigation) ||
        (base->sdkVersion != 0 && base->sdkVersion > requested->sdkVersion) ||
        (base->minorVersion != 0 && base->minorVersion != requested->minorVersion)) {
        result = 0;
    }

    so_logd("AConfiguration_match(%p,%p): %d", base, requested, result);
    return result;
}

// 与 requested 的差值之和；未指定的维度按 0 算，差值自然变大，等于惩罚
static int dp_delta(const AConfiguration *config, const AConfiguration *requested) {
    int delta = 0;
    if (requested->screenWidthDp) {
        delta += abs((int)config->screenWidthDp - (int)requested->screenWidthDp);
    }
    if (requested->screenHeightDp) {
        delta += abs((int)config->screenHeightDp - (int)requested->screenHeightDp);
    }
    if (requested->screenWidth) {
        delta += abs((int)config->screenWidth - (int)requested->screenWidth);
    }
    if (requested->screenHeight) {
        delta += abs((int)config->screenHeight - (int)requested->screenHeight);
    }
    return delta;
}

/*
 * 密度比较：任何密度都能用（系统会缩放），但缩小比放大好，所以取离 requested
 * 更近的；两边对称时优先小的。
 */
static int density_is_better(const AConfiguration *me, const AConfiguration *other,
                             const AConfiguration *requested) {
    int h = me->density ? me->density : ACONFIGURATION_DENSITY_MEDIUM;
    int l = other->density ? other->density : ACONFIGURATION_DENSITY_MEDIUM;
    int i_am_bigger = 1;
    if (l > h) {
        int t = h;
        h = l;
        l = t;
        i_am_bigger = 0;
    }

    int req = requested->density ? requested->density : ACONFIGURATION_DENSITY_MEDIUM;
    if (req >= h) {
        return i_am_bigger;
    }
    if (l >= req) {
        return !i_am_bigger;
    }
    // 缩小的收益按两倍算
    return ((2 * l) - req) * h > req * req ? !i_am_bigger : i_am_bigger;
}

/*
 * me 和 other 都通过 match() 之后，比较谁更贴合 requested。按 Android 的优先级
 * 从高到低逐级比较：mcc/mnc → 语言地区 → 布局方向 → 尺寸 → 方向 → uiMode →
 * 密度 → 输入设备 → 版本。某级分出胜负就返回，全平返回 0。
 */
static int32_t better_than(const AConfiguration *me, const AConfiguration *other,
                           const AConfiguration *requested) {
    if (me->mcc != other->mcc && requested->mcc) {
        return me->mcc != 0;
    }
    if (me->mnc != other->mnc && requested->mnc) {
        return me->mnc != 0;
    }
    if (me->language[0] != other->language[0] && requested->language[0]) {
        return me->language[0] != 0;
    }
    if (me->country[0] != other->country[0] && requested->country[0]) {
        return me->country[0] != 0;
    }
    if (me->layoutDirection != other->layoutDirection && requested->layoutDirection) {
        return me->layoutDirection > other->layoutDirection;
    }
    // 更小的尺寸已被 match() 过滤掉，这里剩下的取大的（更贴近实际）
    if (me->smallestScreenWidthDp != other->smallestScreenWidthDp) {
        return me->smallestScreenWidthDp > other->smallestScreenWidthDp;
    }
    int my_delta    = dp_delta(me, requested);
    int other_delta = dp_delta(other, requested);
    if (my_delta != other_delta) {
        return my_delta < other_delta;
    }
    // 屏幕尺寸枚举：0 视作 normal；归一后相等时「明确写了」的那个更优
    if (me->screenSize != other->screenSize) {
        int my_size    = me->screenSize ? me->screenSize : ACONFIGURATION_SCREENSIZE_NORMAL;
        int other_size = other->screenSize ? other->screenSize : ACONFIGURATION_SCREENSIZE_NORMAL;
        if (my_size == other_size) {
            return me->screenSize != 0;
        }
        return my_size > other_size;
    }
    if (me->screenLong != other->screenLong && requested->screenLong) {
        return me->screenLong != 0;
    }
    if (me->orientation != other->orientation && requested->orientation) {
        return me->orientation != 0;
    }
    if (me->uiModeType != other->uiModeType && requested->uiModeType) {
        return me->uiModeType != 0;
    }
    if (me->uiModeNight != other->uiModeNight && requested->uiModeNight) {
        return me->uiModeNight != 0;
    }
    if (me->density != other->density) {
        return density_is_better(me, other, requested);
    }
    if (me->touchscreen != other->touchscreen && requested->touchscreen) {
        return me->touchscreen != 0;
    }
    // 键盘/导航隐藏：requested 指定了就按它判，没指定则「明确写了」的更优
    if (me->keysHidden != other->keysHidden) {
        if (requested->keysHidden) {
            if (me->keysHidden == requested->keysHidden) {
                return 1;
            }
            if (other->keysHidden == requested->keysHidden) {
                return 0;
            }
        }
        return me->keysHidden != 0;
    }
    if (me->navHidden != other->navHidden) {
        if (requested->navHidden) {
            if (me->navHidden == requested->navHidden) {
                return 1;
            }
            if (other->navHidden == requested->navHidden) {
                return 0;
            }
        }
        return me->navHidden != 0;
    }
    if (me->keyboard != other->keyboard && requested->keyboard) {
        return me->keyboard != 0;
    }
    if (me->navigation != other->navigation && requested->navigation) {
        return me->navigation != 0;
    }
    if (me->sdkVersion != other->sdkVersion && requested->sdkVersion) {
        return me->sdkVersion > other->sdkVersion;
    }
    if (me->minorVersion != other->minorVersion && requested->minorVersion) {
        return me->minorVersion != 0;
    }
    return 0;
}

// requested 为 NULL 时退化成「谁更具体」：按同一优先级看谁在某维度上给出了值
static int32_t more_specific_than(const AConfiguration *me, const AConfiguration *other) {
    if (me->mcc != other->mcc) {
        return me->mcc != 0 && other->mcc == 0;
    }
    if (me->mnc != other->mnc) {
        return me->mnc != 0 && other->mnc == 0;
    }
    if (me->language[0] != other->language[0]) {
        return me->language[0] != 0 && other->language[0] == 0;
    }
    if (me->country[0] != other->country[0]) {
        return me->country[0] != 0 && other->country[0] == 0;
    }
    if (me->layoutDirection != other->layoutDirection) {
        return me->layoutDirection != 0 && other->layoutDirection == 0;
    }
    if (me->smallestScreenWidthDp != other->smallestScreenWidthDp) {
        return me->smallestScreenWidthDp != 0 && other->smallestScreenWidthDp == 0;
    }
    if (me->screenWidthDp != other->screenWidthDp) {
        return me->screenWidthDp != 0 && other->screenWidthDp == 0;
    }
    if (me->screenHeightDp != other->screenHeightDp) {
        return me->screenHeightDp != 0 && other->screenHeightDp == 0;
    }
    if (me->screenSize != other->screenSize) {
        return me->screenSize != 0 && other->screenSize == 0;
    }
    if (me->screenLong != other->screenLong) {
        return me->screenLong != 0 && other->screenLong == 0;
    }
    if (me->orientation != other->orientation) {
        return me->orientation != 0 && other->orientation == 0;
    }
    if (me->uiModeType != other->uiModeType) {
        return me->uiModeType != 0 && other->uiModeType == 0;
    }
    if (me->uiModeNight != other->uiModeNight) {
        return me->uiModeNight != 0 && other->uiModeNight == 0;
    }
    if (me->density != other->density) {
        return me->density != 0 && other->density == 0;
    }
    if (me->touchscreen != other->touchscreen) {
        return me->touchscreen != 0 && other->touchscreen == 0;
    }
    if (me->keysHidden != other->keysHidden) {
        return me->keysHidden != 0 && other->keysHidden == 0;
    }
    if (me->keyboard != other->keyboard) {
        return me->keyboard != 0 && other->keyboard == 0;
    }
    if (me->navHidden != other->navHidden) {
        return me->navHidden != 0 && other->navHidden == 0;
    }
    if (me->navigation != other->navigation) {
        return me->navigation != 0 && other->navigation == 0;
    }
    if (me->sdkVersion != other->sdkVersion) {
        return me->sdkVersion != 0 && other->sdkVersion == 0;
    }
    return 0;
}

/*
 * 参数顺序跟 Android 实现一致：判定的是「base 是否优于 test」（AOSP 的
 * AConfiguration_isBetterThan 就是 base->isBetterThan(*test)；头文件注释写成
 * 「test 是否优于 base」，两者矛盾，这里跟代码走）。
 */
int32_t AConfiguration_isBetterThan(AConfiguration *base, AConfiguration *test,
                                    AConfiguration *requested) {
    if (!base || !test) {
        return 0;
    }

    int32_t result = requested ? better_than(base, test, requested)
                               : more_specific_than(base, test);
    so_logd("AConfiguration_isBetterThan(%p,%p,%p): %d", base, test, requested, result);
    return result;
}
