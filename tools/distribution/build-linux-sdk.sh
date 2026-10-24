#!/usr/bin/env bash
# Debian 13 native OCCT SDK. Qt/OpenSSL and build tools come from Debian packages.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
sdk_root="$root/build/native-sdk"
archive="$sdk_root/occt-8.0.1.tar.gz"
mkdir -p "$sdk_root"
if [[ ! -f "$archive" ]]; then
    curl -L --fail https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.tar.gz -o "$archive"
fi
printf '%s  %s\n' 0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a "$archive" | sha256sum --check
if [[ ! -d "$sdk_root/OCCT-8_0_1" ]]; then tar -xzf "$archive" -C "$sdk_root"; fi
gauss_patch="$root/tools/dependencies/occt-gauss-large-spans.patch"
gauss_source=build/native-sdk/OCCT-8_0_1
if git -C "$root" apply --directory="$gauss_source" --check "$gauss_patch"; then
    git -C "$root" apply --directory="$gauss_source" "$gauss_patch"
elif ! git -C "$root" apply --directory="$gauss_source" --reverse --check "$gauss_patch"; then
    printf 'OCCT source does not match the required Gauss interval patch.\n' >&2
    exit 1
fi
cmake -S "$sdk_root/OCCT-8_0_1" -B "$sdk_root/occt-8.0.1-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$sdk_root/occt-8.0.1" \
    -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_Visualization=OFF \
    -DUSE_TCL=OFF -DUSE_TK=OFF -DUSE_TBB=OFF -DUSE_FREETYPE=OFF \
    -DBUILD_DOC_Overview=OFF -DCMAKE_INSTALL_RPATH='$ORIGIN' -DINSTALL_DIR_LAYOUT=Unix -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON
cmake --build "$sdk_root/occt-8.0.1-build" --parallel "${ZIMA_BUILD_JOBS:-12}"
cmake --install "$sdk_root/occt-8.0.1-build"
gauss_patch_sha=$(sha256sum "$gauss_patch" | cut -d ' ' -f 1)
mkdir -p "$sdk_root/occt-8.0.1/share/opencascade"
printf '{"version":"8.0.1","gauss_large_spans_patch_sha256":"%s"}\n' "$gauss_patch_sha" \
    > "$sdk_root/occt-8.0.1/share/opencascade/zima-sdk.json"
cat > "$sdk_root/occt-8.0.1/sdk.json" <<'JSON'
{"name":"OCCT","version":"8.0.1","source":"https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.tar.gz","sha256":"0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a","configuration":"Release; Draw/Visualization/Tcl/Tk/TBB/FreeType disabled"}
JSON
