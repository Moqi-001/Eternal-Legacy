#
# so_loader 模板工程的构建脚本
#
# vendor/ 是加载器源码，source/ 是移植者要改的代码，
# libs/ 是预编译库，resources/ 是随包分发的资源。
# 用法：把整个仓库复制成 <游戏名>/，然后只改 source/ 和下面标了「改这里」的几项。
# 详细说明见 docs/PORTING.md。
#
cmake_minimum_required(VERSION 3.14)

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Please define VITASDK env var to point to your SDK path or set CMAKE_TOOLCHAIN_FILE.")
endif ()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

project(Runtime C CXX)

# ==== Vita 应用信息（改这里）====
set(VITA_APP_NAME "Eternal Legacy")
set(VITA_TITLEID  "ETERNAL00")     # 9 位
set(VITA_VPKNAME  "EternalLegacy")
set(VITA_VERSION  "01.00")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

# Optional: used for "send" target
set(PSVITAIP "192.168.1.100" CACHE STRING "PSVita IP (for FTP access)")

# ==== 游戏数据路径（改这里）====
set(DATA_PATH "ux0:data/EternalLegacy/" CACHE STRING "Path to data (with trailing /)")
add_definitions(-DDATA_PATH="${DATA_PATH}" -DLOG_PATH="${LOG_PATH}")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG_SOLOADER)
endif ()

option(DEBUG_LOGFILE "Also write loader logs to a file" OFF)
if (DEBUG_LOGFILE)
  set(LOG_PATH "${DATA_PATH}log.txt" CACHE STRING "Path to log.txt")
  add_definitions(-DDEBUG_LOGFILE -DLOG_PATH="${LOG_PATH}")
endif()

option(DEBUG_OBSERVE "Enable verbose engine and GL tracing" OFF)
if (DEBUG_OBSERVE)
  add_definitions(-DDEBUG_OBSERVE)
endif()

# makes sincos, sincosf, etc. visible
add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu11 -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu++20 -Wno-write-strings -Wno-psabi")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -g3")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -g3")
else ()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g0 -ffast-math")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g0 -ffast-math")
endif ()

enable_language(ASM)

# ==== 框架（必需）====
file(GLOB_RECURSE SO_COMMON_SRC vendor/so_common/*.c)
file(GLOB_RECURSE SO_LINKER_SRC vendor/so_linker/*.c)
file(GLOB_RECURSE SO_JAVA_SRC   vendor/so_java/*.c vendor/so_java/*.cpp)
file(GLOB_RECURSE SO_JNI_SRC    vendor/so_jni/*.c vendor/so_jni/*.S)

# ==== 虚拟库 ====
file(GLOB_RECURSE SO_LIBLOG_SRC           vendor/so_libs/liblog/*.c)
file(GLOB_RECURSE SO_LIBZ_SRC             vendor/so_libs/libz/*.c)
file(GLOB_RECURSE SO_LIBM_SRC             vendor/so_libs/libm/*.c)
file(GLOB_RECURSE SO_LIBDL_SRC            vendor/so_libs/libdl/*.c)
file(GLOB_RECURSE SO_LIBEGL_SRC           vendor/so_libs/libEGL/*.c)
file(GLOB_RECURSE SO_LIBGLESV1_SRC        vendor/so_libs/libGLESv1_CM/*.c)
file(GLOB_RECURSE SO_LIBGLESV2_SRC        vendor/so_libs/libGLESv2/*.c)
file(GLOB_RECURSE SO_LIBANDROID_SRC       vendor/so_libs/libandroid/*.c)
file(GLOB_RECURSE SO_LIBCUTILS_SRC        vendor/so_libs/libcutils/*.c)
file(GLOB_RECURSE SO_LIBUTILS_SRC         vendor/so_libs/libutils/*.c)
file(GLOB_RECURSE SO_LIBC_SRC             vendor/so_libs/libc/*.c)
file(GLOB_RECURSE SO_LIBSTDCXX_SRC        vendor/so_libs/libstdcxx/*.c)

# ==== 项目代码：source/* ====
file(GLOB_RECURSE SOURCE_SRC source/*.c)

add_executable(${CMAKE_PROJECT_NAME}
  ${SO_COMMON_SRC}
  ${SO_LINKER_SRC}
  ${SO_JAVA_SRC}
  ${SO_JNI_SRC}

  ${SO_LIBLOG_SRC}
  ${SO_LIBZ_SRC}
  ${SO_LIBM_SRC}
  ${SO_LIBDL_SRC}
  ${SO_LIBEGL_SRC}
  ${SO_LIBGLESV1_SRC}
  ${SO_LIBGLESV2_SRC}
  ${SO_LIBANDROID_SRC}
  ${SO_LIBCUTILS_SRC}
  ${SO_LIBUTILS_SRC}
  ${SO_LIBC_SRC}
  ${SO_LIBSTDCXX_SRC}

  ${SOURCE_SRC}
)

target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
  PUBLIC ${CMAKE_SOURCE_DIR}/libs/include
  PUBLIC ${CMAKE_SOURCE_DIR}/libs/include/freetype2
  PUBLIC ${CMAKE_SOURCE_DIR}/vendor
  PUBLIC ${CMAKE_SOURCE_DIR}/vendor/so_libs
  PUBLIC ${CMAKE_SOURCE_DIR}/extras
)

target_include_directories(${CMAKE_PROJECT_NAME}
  PUBLIC ${CMAKE_SOURCE_DIR}/source
)

target_link_directories(${CMAKE_PROJECT_NAME}
  PUBLIC ${CMAKE_SOURCE_DIR}/libs/lib
)

# Keep pthread-embedded's key/destructor handling and replace its Vita TLS storage.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
  -Wl,--wrap=pte_osTlsAlloc
  -Wl,--wrap=pte_osTlsFree
  -Wl,--wrap=pte_osTlsGetValue
  -Wl,--wrap=pte_osTlsSetValue
)

target_link_libraries(${CMAKE_PROJECT_NAME}
  -Wl,--whole-archive pthread -Wl,--no-whole-archive
  vitaGL
  vitashark
  SceShaccCgExt
  taihen_stub
  SceShaccCg_stub
  kubridge_stub
  mathneon
  speexdsp
  sqlite3
  freetype
  png
  bz2
  z
  m
  stdc++
  SceHid_stub
  SceCtrl_stub
  SceTouch_stub
  SceMotion_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceAudio_stub
  SceAudioIn_stub
  SceCommonDialog_stub
  SceDisplay_stub
  SceKernelDmacMgr_stub
  SceGxm_stub
  ScePower_stub
  SceVshBridge_stub
  SceAvPlayer_stub
)

vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE NOASLR)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  # 根目录这三个对应 libc 对 Android 的模拟：/proc/cpuinfo、/proc/meminfo、/dev/urandom
  FILE ${CMAKE_SOURCE_DIR}/resources/cpuinfo    cpuinfo
  FILE ${CMAKE_SOURCE_DIR}/resources/meminfo    meminfo
  FILE ${CMAKE_SOURCE_DIR}/resources/urandom    urandom
  # 气泡图标与 LiveArea
  FILE ${CMAKE_SOURCE_DIR}/resources/livearea/icon0.png     sce_sys/icon0.png
  FILE ${CMAKE_SOURCE_DIR}/resources/livearea/pic0.png      sce_sys/pic0.png
  FILE ${CMAKE_SOURCE_DIR}/resources/livearea/startup.png   sce_sys/livearea/contents/startup.png
  FILE ${CMAKE_SOURCE_DIR}/resources/livearea/bg0.png       sce_sys/livearea/contents/bg0.png
  FILE ${CMAKE_SOURCE_DIR}/resources/livearea/template.xml  sce_sys/livearea/contents/template.xml
)

add_custom_target(send
  COMMAND echo destroy | nc ${PSVITAIP} 1338
  COMMAND echo destroy | nc ${PSVITAIP} 1338
  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
  COMMAND echo launch ${VITA_TITLEID} | nc ${PSVITAIP} 1338
  DEPENDS ${VITA_VPKNAME}.vpk-vpk
)
