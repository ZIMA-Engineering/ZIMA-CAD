#!/usr/bin/env bash
# Debian 13 native OCCT SDK. Qt/OpenSSL and build tools come from Debian packages.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
sdk_root="$root/build/native-sdk"
archive="$sdk_root/occt-7.9.3.tar.gz"
mkdir -p "$sdk_root"
if [[ ! -f "$archive" ]]; then
    curl -L --fail https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_9_3.tar.gz -o "$archive"
fi
printf '%s  %s\n' 5ecf094ec6b12d5413dfb851d8c3590c354058aee556e32e408bdfbf8c357d57 "$archive" | sha256sum --check
if [[ ! -d "$sdk_root/OCCT-7_9_3" ]]; then tar -xzf "$archive" -C "$sdk_root"; fi
cmake -S "$sdk_root/OCCT-7_9_3" -B "$sdk_root/occt-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$sdk_root/occt" \
    -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_Visualization=OFF \
    -DUSE_TCL=OFF -DUSE_TK=OFF -DUSE_TBB=OFF -DUSE_FREETYPE=OFF \
    -DBUILD_DOC_Overview=OFF -DCMAKE_INSTALL_RPATH='$ORIGIN' -DINSTALL_DIR_LAYOUT=Unix -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON
cmake --build "$sdk_root/occt-build" --parallel "${ZIMA_BUILD_JOBS:-12}"
cmake --install "$sdk_root/occt-build"
cat > "$sdk_root/occt/sdk.json" <<'JSON'
{"name":"OCCT","version":"7.9.3","source":"https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_9_3.tar.gz","sha256":"5ecf094ec6b12d5413dfb851d8c3590c354058aee556e32e408bdfbf8c357d57","configuration":"Release; Draw/Visualization/Tcl/Tk/TBB/FreeType disabled"}
JSON
