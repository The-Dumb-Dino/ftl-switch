cmake_minimum_required(VERSION 3.20)
project(ftlport C ASM)

set(CMAKE_C_STANDARD 11)
set(LIBCXX_DIR "" CACHE PATH "Apple-ABI libc++ install dir (lib/ with libc++.a, libc++abi.a, libunwind.a)")
option(FTL_SWITCH_APP "Build the Nintendo Switch homebrew app (ftl.nro; switch/build.sh)" OFF)
option(FTL_HARNESS "Build the headless test harness (ftlrun; native/build-harness.sh)" OFF)

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

if(FTL_SWITCH_APP)
    # Switch homebrew (switch/toolchain.cmake, README in switch/). The POSIX execmem and
    # Hyperspace's dlopen don't apply: code is mapped with svcMapProcessCodeMemory.
    set(FTL_SWITCH_VERSION "dev" CACHE STRING "Version string of ftl.nro")
    # Hyperspace, linked in (hyperspace/build-switch.sh): its library and the folder with
    # the Lua it was built with. Without it, ftl.nro has stubs and plays without Hyperspace.
    set(FTL_SWITCH_HYPERSPACE "" CACHE FILEPATH "libHyperspace.a built for the Switch (optional)")
    set(FTL_SWITCH_HS_DEPS "" CACHE PATH "Its dependencies (lib/liblua.a)")
    # ftlman's patch engine, linked in (switch/rust/build-ftlman.sh): mods from /switch/ftl/mods/
    set(FTL_SWITCH_FTLMAN "" CACHE FILEPATH "libftlman_switch.a built for the Switch (optional)")
    # The built-in updater, for release builds only (RELEASE=1 switch/build.sh): the
    # GitHub repository (owner/name) whose latest release it offers. Empty: no updater.
    set(FTL_SWITCH_UPDATE_REPO "" CACHE STRING "GitHub repository of the releases the updater offers (release builds)")
    list(REMOVE_ITEM CORE_SOURCES src/loader/execmem.c)
    if(NOT FTL_SWITCH_HYPERSPACE)
        list(REMOVE_ITEM CORE_SOURCES src/hyperspace.c)
    endif()
    add_executable(ftl ${CORE_SOURCES}
        src/platform/switch/main_switch.c
        src/platform/switch/plat_switch.c
        src/platform/switch/execmem_switch.c
        src/platform/switch/log_switch.c
        src/platform/switch/compat_switch.c
        src/platform/switch/memdiag_switch.c
        # shared code from the Linux version: settings.ini, the .ipa import, the mod image overlay
        src/platform/linux/settings.c
        src/platform/linux/import.c
        src/platform/linux/zip.c
        src/platform/linux/mods_linux.c)
    set_target_properties(ftl PROPERTIES SUFFIX ".elf")
    # _GNU_SOURCE (empty, as the files that also define it do): newlib shows vasprintf
    # and the like only then, and it must come before the stdint.h included below
    target_compile_definitions(ftl PRIVATE FTL_SWITCH_APP _GNU_SOURCE= FTL_SWITCH_VERSION="${FTL_SWITCH_VERSION}")
    # every allocation through memdiag_switch.c, which logs the ones that fail
    target_link_options(ftl PRIVATE -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=memalign)
    if(FTL_SWITCH_FTLMAN)
        # One object with only ftlman_patch global: its Lua 5.4 (and Rust's runtime)
        # stay private next to Hyperspace's Lua 5.3, as mesa's libstdc++ does above.
        add_custom_command(OUTPUT ftlman-private.o
            COMMAND ld.lld -r -o ftlman-linked.o --whole-archive ${FTL_SWITCH_FTLMAN}
            COMMAND llvm-objcopy --keep-global-symbol=ftlman_patch ftlman-linked.o ftlman-private.o
            DEPENDS ${FTL_SWITCH_FTLMAN} VERBATIM)
        add_custom_target(ftlman_private DEPENDS ftlman-private.o)
        add_dependencies(ftl ftlman_private)
        target_sources(ftl PRIVATE src/platform/switch/mods_switch.c)
        target_compile_definitions(ftl PRIVATE FTL_SWITCH_FTLMAN)
        target_link_libraries(ftl PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ftlman-private.o)
    endif()
    if(FTL_SWITCH_UPDATE_REPO)
        target_sources(ftl PRIVATE src/platform/switch/update_switch.c)
        target_compile_definitions(ftl PRIVATE FTL_SWITCH_UPDATE_REPO="${FTL_SWITCH_UPDATE_REPO}" CURL_STATICLIB)
        target_link_libraries(ftl PRIVATE ${DEVKITPRO}/portlibs/switch/lib/libcurl.a)
    endif()
    if(FTL_SWITCH_HYPERSPACE)
        target_compile_definitions(ftl PRIVATE FTL_SWITCH_HYPERSPACE)
        # before libc++: Hyperspace uses the same one as the game. Whole: most of its
        # files only register hooks from static constructors, which nothing references,
        # so a plain archive link left a third of them out (the more-info button, lazy
        # image loading, custom tooltips and more).
        target_link_libraries(ftl PRIVATE -Wl,--whole-archive ${FTL_SWITCH_HYPERSPACE} -Wl,--no-whole-archive
            ${FTL_SWITCH_HS_DEPS}/lib/liblua.a
            ${DEVKITPRO}/portlibs/switch/lib/libminizip.a)
    endif()
    target_include_directories(ftl PRIVATE include src)
    # newlib's stdatomic.h (which clang's defers to) uses stdint.h's types without including it
    target_compile_options(ftl PRIVATE ${COMMON_FLAGS} -O2 -g -Wno-cast-function-type-mismatch
        -Wno-gnu-variable-sized-type-not-at-end -Wno-unknown-warning-option
        $<$<COMPILE_LANGUAGE:C>:-include stdint.h>)
    # mesa for the Switch (devkitPro's switch-mesa: EGL, GLESv2, glapi, drm_nouveau). Its GPU
    # compiler is C++ built against libstdc++, whose std::exception & co. clash with the
    # game's libc++. So mesa and the libstdc++ it needs are linked into one object first,
    # in which everything but mesa's API (egl*, gl*) is made local: mesa keeps a private
    # C++ runtime and the game's libc++ stays as it is.
    set(_pl ${DEVKITPRO}/portlibs/switch/lib)
    set(_nl ${DEVKITPRO}/devkitA64/aarch64-none-elf/lib)
    add_custom_command(OUTPUT mesa-private.o
        COMMAND ld.lld -r -o mesa-linked.o --whole-archive ${_pl}/libEGL.a ${_pl}/libGLESv2.a ${_pl}/libglapi.a
                ${_pl}/libdrm_nouveau.a --no-whole-archive ${_nl}/libstdc++.a
        COMMAND llvm-objcopy --wildcard --keep-global-symbol=egl* --keep-global-symbol=gl*
                mesa-linked.o mesa-private.o
        DEPENDS ${_pl}/libEGL.a ${_pl}/libGLESv2.a ${_pl}/libglapi.a ${_pl}/libdrm_nouveau.a VERBATIM)
    add_custom_target(mesa_private DEPENDS mesa-private.o)
    add_dependencies(ftl mesa_private)
    target_link_libraries(ftl PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/mesa-private.o ${CXX_LIBS} z)
    find_program(ELF2NRO elf2nro HINTS $ENV{HOME}/ftl-build/switch/tools ${DEVKITPRO}/tools/bin REQUIRED)
    find_program(NACPTOOL nacptool HINTS $ENV{HOME}/ftl-build/switch/tools ${DEVKITPRO}/tools/bin REQUIRED)
    set(FTL_SWITCH_ICON "" CACHE FILEPATH "256x256 JPEG for the Homebrew Menu (optional; not in the repository)")
    if(FTL_SWITCH_ICON)
        set(_icon --icon=${FTL_SWITCH_ICON})
        set(_icon_dep ${FTL_SWITCH_ICON})
    endif()
    # a relocation into read-only memory crashes ftl.nro on a console before main
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT ftl.nro
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../switch/check-relocs.py $<TARGET_FILE:ftl>
        COMMAND ${NACPTOOL} --create "FTL: Faster Than Light" "FTL port (your own iPad copy)" "${FTL_SWITCH_VERSION}" ftl.nacp
        COMMAND ${ELF2NRO} $<TARGET_FILE:ftl> ftl.nro --nacp=ftl.nacp ${_icon}
        DEPENDS ftl ${_icon_dep} VERBATIM)
    add_custom_target(nro ALL DEPENDS ftl.nro)
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
    message(FATAL_ERROR "Build with switch/build.sh (ftl.nro) or native/build-harness.sh (the test harness)")
endif()
