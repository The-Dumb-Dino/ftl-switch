# Nintendo Switch homebrew (libnx) with the host's clang and lld instead of
# devkitA64's gcc: the target libraries (newlib, libnx, mesa, SDL2) come from
# devkitPro's packages, which don't depend on the host (switch/setup.sh fetches
# them into DEVKITPRO). Works on any host clang supports, aarch64 Linux included,
# where devkitPro has no compiler.
#   DEVKITPRO: the devkitPro tree (default ~/ftl-build/switch/devkitpro)
if(DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO "$ENV{DEVKITPRO}")
else()
    set(DEVKITPRO "$ENV{HOME}/ftl-build/switch/devkitpro")
endif()
set(DEVKITPRO "${DEVKITPRO}" CACHE PATH "devkitPro tree")
set(ENV{DEVKITPRO} "${DEVKITPRO}")  # for try_compile

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(NINTENDO_SWITCH ON)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_CXX_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_ASM_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_newlib "${DEVKITPRO}/devkitA64/aarch64-none-elf")
file(GLOB _gcc_dirs "${DEVKITPRO}/devkitA64/lib/gcc/aarch64-none-elf/*")
list(GET _gcc_dirs 0 SWITCH_GCC_LIB)
execute_process(COMMAND clang -print-resource-dir OUTPUT_VARIABLE _clang_res OUTPUT_STRIP_TRAILING_WHITESPACE)

# devkitA64's flags, except: thread-locals are emulated (-femulated-tls, through
# libgcc's emutls) since libnx keeps the thread pointer in memory (gcc's
# -mtp=soft), which clang can't read directly.
string(CONCAT _switch_flags
    " -march=armv8-a+crc+crypto -mtune=cortex-a57 -fPIE -femulated-tls"
    " -ffunction-sections -fdata-sections -D__SWITCH__"
    " -nostdinc -isystem ${_clang_res}/include"
    " -isystem ${DEVKITPRO}/libnx/include -isystem ${DEVKITPRO}/portlibs/switch/include"
    " -isystem ${_newlib}/include"
)
# Appended directly rather than through _INIT, so flags given on the command line
# (CMAKE_C_FLAGS=...) add to them instead of replacing them.
if(NOT _SWITCH_TOOLCHAIN)
    set(_SWITCH_TOOLCHAIN 1)
    foreach(lang C CXX ASM)
        string(APPEND CMAKE_${lang}_FLAGS "${_switch_flags}")
    endforeach()
    # The C++ standard library comes from the caller (libc++ in its build, or ours).
    string(APPEND CMAKE_CXX_FLAGS " -nostdinc++")
endif()

set(SWITCH_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/switch-lld.ld")
set(CMAKE_LINKER ld.lld)
string(CONCAT _switch_link
    " -fuse-ld=lld -nostdlib -nostartfiles -static-pie"
    " -Wl,-T,${SWITCH_LINKER_SCRIPT} -Wl,-pie -Wl,--no-dynamic-linker -Wl,-z,notext -Wl,-z,now"
    " -Wl,-z,pack-relative-relocs -Wl,--gc-sections -Wl,--build-id=sha1 -Wl,-u,main"
    # libc++'s chrono asks for librt in the object file; newlib has it all in libc
    " -Wl,--no-dependent-libraries"
    " -L${DEVKITPRO}/portlibs/switch/lib -L${DEVKITPRO}/libnx/lib -L${_newlib}/lib -L${SWITCH_GCC_LIB}"
)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_switch_link}")
set(SWITCH_CRT_BEGIN "${SWITCH_GCC_LIB}/crti.o ${SWITCH_GCC_LIB}/crtbegin.o")
set(SWITCH_CRT_END "${SWITCH_GCC_LIB}/crtend.o ${SWITCH_GCC_LIB}/crtn.o")
# crt files around the objects, the system libraries after them
set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> ${SWITCH_CRT_BEGIN} <OBJECTS> -o <TARGET> <LINK_LIBRARIES> -Wl,--start-group -lnx -lc -lm -lgcc -lsysbase -Wl,--end-group ${SWITCH_CRT_END}")
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> ${SWITCH_CRT_BEGIN} <OBJECTS> -o <TARGET> <LINK_LIBRARIES> -Wl,--start-group -lnx -lc -lm -lgcc -lsysbase -Wl,--end-group ${SWITCH_CRT_END}")

set(CMAKE_FIND_ROOT_PATH "${DEVKITPRO}/portlibs/switch" "${DEVKITPRO}/libnx" "${_newlib}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
