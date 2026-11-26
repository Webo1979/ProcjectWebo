#!/usr/bin/env bash
set -euo pipefail

if [[ ${MSYSTEM:-} != MINGW64 ]]; then
    printf '%s\n' 'Use the MSYS2 MINGW64 shell.' >&2
    exit 1
fi
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
source_dir="$PWD"
out="$PWD/build"
prefix="$out/gensio-install"
revision=36828e3a209f0d7be284d9cc53c4b28cd7c1eb6a
mkdir -p "$out"
if [[ ! -d "$out/gensio/.git" ]]; then
    git clone https://github.com/cminyard/gensio.git "$out/gensio"
fi
git -C "$out/gensio" checkout --detach "$revision"

# At this revision, the test Meson file assumes Python bindings are enabled.
sed -i "/^subdir('tests')/d" "$out/gensio/meson.build"
if [[ ! -f "$out/gensio-build/build.ninja" ]]; then
    meson setup "$out/gensio-build" "$out/gensio" --prefix "$(cygpath -am "$prefix")" \
        --libdir lib --buildtype release --auto-features disabled \
        -Ddocs=false -Dcplusplus=false -Dall-gensios=no -Dgensio-net=yes -Dgensio-dgram=yes
fi
meson compile -C "$out/gensio-build"
meson install -C "$out/gensio-build"

cmake -S "$source_dir" -B "$out/app" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    "-DGENSIO_ROOT=$(cygpath -am "$prefix")" "-DCMAKE_PREFIX_PATH=$(cygpath -am /mingw64)"
cmake --build "$out/app"
mkdir -p "$out/portable"
cp "$out/app/GensioNetDebug.exe" "$prefix"/bin/libgensio{,osh}-*.dll "$out/portable/"
for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
    cp "/mingw64/bin/$dll" "$out/portable/"
done
windeployqt --release --no-translations --no-opengl-sw "$out/portable/GensioNetDebug.exe"
