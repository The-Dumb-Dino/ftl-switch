#!/bin/bash
# The tools Hyperspace's build runs, built from source into ~/ftl-build/hs-tools so
# no system packages are needed: Lua 5.3 with lpeg and lfs (its code generator)
# and swig 4.0 (its Lua bindings), the versions of Hyperspace's build container.
#   hyperspace/build-tools.sh     -> ~/ftl-build/hs-tools/{bin,lib/lua/5.3}
set -euo pipefail
TOOLS=${HS_TOOLS:-$HOME/ftl-build/hs-tools}
WORK=$TOOLS/src
mkdir -p "$WORK" "$TOOLS/bin" "$TOOLS/lib/lua/5.3"
cd "$WORK"
fetch() { [ -f "$2" ] || curl -fsSL -o "$2" "$1"; }

if [ ! -x "$TOOLS/bin/lua" ]; then
  fetch https://www.lua.org/ftp/lua-5.3.6.tar.gz lua-5.3.6.tar.gz
  rm -rf lua-5.3.6 && tar xf lua-5.3.6.tar.gz
  # Linux, without readline (no -devel package needed); C modules load with dlopen
  make -C lua-5.3.6/src all CC=cc SYSCFLAGS="-DLUA_USE_POSIX -DLUA_USE_DLOPEN" SYSLIBS="-Wl,-E -ldl" >/dev/null
  cp lua-5.3.6/src/lua lua-5.3.6/src/luac "$TOOLS/bin/"
  mkdir -p "$TOOLS/include/lua5.3"
  cp lua-5.3.6/src/{lua.h,luaconf.h,lualib.h,lauxlib.h} "$TOOLS/include/lua5.3/"
fi
LUAINC=$TOOLS/include/lua5.3

if [ ! -f "$TOOLS/lib/lua/5.3/lpeg.so" ]; then
  fetch https://www.inf.puc-rio.br/~roberto/lpeg/lpeg-1.0.2.tar.gz lpeg-1.0.2.tar.gz
  rm -rf lpeg-1.0.2 && tar xf lpeg-1.0.2.tar.gz
  cc -O2 -fPIC -shared -I"$LUAINC" lpeg-1.0.2/*.c -o "$TOOLS/lib/lua/5.3/lpeg.so"
fi

if [ ! -f "$TOOLS/lib/lua/5.3/lfs.so" ]; then
  fetch https://github.com/lunarmodules/luafilesystem/archive/refs/tags/v1_8_0.tar.gz lfs-1.8.0.tar.gz
  rm -rf luafilesystem-1_8_0 && tar xf lfs-1.8.0.tar.gz
  cc -O2 -fPIC -shared -I"$LUAINC" luafilesystem-1_8_0/src/lfs.c -o "$TOOLS/lib/lua/5.3/lfs.so"
fi

if [ ! -x "$TOOLS/bin/swig" ]; then
  fetch https://downloads.sourceforge.net/project/swig/swig/swig-4.0.2/swig-4.0.2.tar.gz swig-4.0.2.tar.gz
  # swig 4.0 needs PCRE (1): its own helper builds a private static copy
  fetch https://downloads.sourceforge.net/project/pcre/pcre/8.45/pcre-8.45.tar.bz2 pcre-8.45.tar.bz2
  rm -rf swig-4.0.2 && tar xf swig-4.0.2.tar.gz
  cp pcre-8.45.tar.bz2 swig-4.0.2/
  (cd swig-4.0.2 && Tools/pcre-build.sh >/dev/null 2>&1 &&
     ./configure --prefix="$TOOLS" --without-alllang >/dev/null &&
     make -j"$(nproc)" >/dev/null && make install >/dev/null)
fi

echo "tools in $TOOLS:"
LUA_CPATH="$TOOLS/lib/lua/5.3/?.so" "$TOOLS/bin/lua" -e 'require("lpeg"); require("lfs"); print("  " .. _VERSION .. " with lpeg and lfs")'
"$TOOLS/bin/swig" -version | grep Version | sed 's/^/  /'
