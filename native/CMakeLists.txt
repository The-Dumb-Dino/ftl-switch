cmake_minimum_required(VERSION 3.20)
project(ftlport C ASM)

set(CMAKE_C_STANDARD 11)
set(LIBCXX_DIR "" CACHE PATH "Apple-ABI libc++ install dir (lib/ with libc++.a, libc++abi.a, libunwind.a)")
option(FTL_HARNESS "Build the Linux test harness executable instead of the Android library" OFF)
option(FTL_LINUX_APP "Build the Linux desktop app (ftl-linux, SDL3) instead of the Android library" OFF)
set(FTL_LINUX_VERSION "dev" CACHE STRING "Version string of ftl-linux")

set(CORE_SOURCES
    src/loader/macho.c
    src/loader/execmem.c
    src/loader/symbols.c
    src/loader/symbols_cxx.c
    src/objc/objc_runtime.c
    src/objc/objc_api.c
    src/objc/objc_msgSend.S
    src/darwin/libsystem.c
    src/darwin/darwin_asm.S
    src/fw/foundation.c
    src/fw/uikit.c
    src/fw/quartz_eagl.c
    src/fw/present.c
    src/fw/pvrtc.c
    src/fw/gamecontroller.c
    src/fw/audio_misc.c
    src/port_main.c
    src/patches.c
    src/hyperspace.c
    src/ux/ux.c
    src/ux/pad.c
    src/ux/targets.c
    src/ux/overlay.c
    src/ux/mods.c
    src/ux/png.c
)

set(COMMON_FLAGS -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
    -fvisibility=hidden -ffunction-sections -fdata-sections)

set(CXX_LIBS
    ${LIBCXX_DIR}/lib/libc++.a
    ${LIBCXX_DIR}/lib/libc++abi.a
    ${LIBCXX_DIR}/lib/libunwind.a)

if(FTL_LINUX_APP)
    find_package(SDL3 REQUIRED CONFIG)
    find_package(ZLIB REQUIRED)
    add_executable(ftl-linux ${CORE_SOURCES}
        src/platform/linux/main_linux.c
        src/platform/linux/plat_sdl.c
        src/platform/linux/settings.c
        src/platform/linux/import.c
        src/platform/linux/zip.c
        src/platform/linux/mods_linux.c
        src/platform/linux/portable.c
        src/platform/linux/log_linux.c
        src/platform/linux/testcmd.c)
    target_compile_definitions(ftl-linux PRIVATE FTL_LINUX_APP FTL_LINUX_VERSION="${FTL_LINUX_VERSION}")
    target_include_directories(ftl-linux PRIVATE include src)
    # newer clang warns about every objc_msgSend cast (the fw/ code calls through it by design)
    target_compile_options(ftl-linux PRIVATE ${COMMON_FLAGS} -O2 -g -Wno-cast-function-type-mismatch
        -Wno-gnu-variable-sized-type-not-at-end -Wno-unknown-warning-option)
    # max-page-size: loadable on 4K, 16K and 64K page kernels
    target_link_options(ftl-linux PRIVATE -fuse-ld=lld -Wl,--exclude-libs,ALL -Wl,--gc-sections
        -Wl,-z,max-page-size=65536)
    target_link_libraries(ftl-linux PRIVATE ${CXX_LIBS} SDL3::SDL3 EGL GLESv2 ZLIB::ZLIB pthread dl m)
    install(TARGETS ftl-linux RUNTIME DESTINATION bin)
    install(FILES linux/ftl-linux.desktop DESTINATION share/applications)
    # ftlman (ftlman/build-linux.sh) goes where ftl-linux looks for it
    set(FTLMAN_BIN "" CACHE FILEPATH "ftlman built for Linux, installed to libexec/ftl-linux")
    if(FTLMAN_BIN)
        install(PROGRAMS ${FTLMAN_BIN} DESTINATION libexec/ftl-linux)
    endif()
    # Hyperspace for the iPad game (hyperspace/build-linux.sh), found there by ftl-linux
    set(HYPERSPACE_SO "" CACHE FILEPATH "Hyperspace built for Linux, installed to libexec/ftl-linux")
    if(HYPERSPACE_SO)
        install(FILES ${HYPERSPACE_SO} DESTINATION libexec/ftl-linux)
    endif()
elseif(FTL_HARNESS)
    add_executable(ftlrun ${CORE_SOURCES} src/platform/plat_linux.c src/debug/guard_alloc.c src/debug/debug_hooks.c)
    target_compile_definitions(ftlrun PRIVATE FTL_GUARD_ALLOC)
    target_include_directories(ftlrun PRIVATE include src)
    target_compile_options(ftlrun PRIVATE ${COMMON_FLAGS} -g)
    target_link_options(ftlrun PRIVATE -fuse-ld=lld -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,-z,max-page-size=65536
        -Wl,--export-dynamic-symbol=free -Wl,--export-dynamic-symbol=realloc
        -Wl,--export-dynamic-symbol=malloc -Wl,--export-dynamic-symbol=calloc)
    target_link_libraries(ftlrun PRIVATE ${CXX_LIBS} EGL GLESv2 z pthread dl m)
else()
    add_library(ftlport SHARED ${CORE_SOURCES} src/platform/plat_android.c src/platform/android_audio.c
        src/platform/log_android.c)
    target_include_directories(ftlport PRIVATE include src)
    target_compile_options(ftlport PRIVATE ${COMMON_FLAGS} -O2 -g)
    target_link_options(ftlport PRIVATE -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,-z,max-page-size=16384
        -unwindlib=none -nostdlib++)
    target_link_libraries(ftlport PRIVATE ${CXX_LIBS} EGL GLESv3 android log aaudio z dl m)
endif()
