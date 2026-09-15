#!/bin/sh
# Portable selection contract; Linux deployment must be verified on Linux.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
read_setting() {
    awk -F= -v section="$2" -v key="$3" '
        { sub(/\r$/, "") }
        /^\[/ { active=($0 == "[" section "]"); next }
        active && $1 == key { sub(/^[^=]*=/, ""); value=$0 }
        END { print value }' "$1"
}
version=''; custom=false; check=false; cli=false
while [ "$#" -gt 0 ]; do
    case "$1" in
        -Version) [ "$#" -ge 2 ] || exit 2; version=$2; shift 2 ;;
        -Custom) custom=true; shift ;;
        -Check) check=true; shift ;;
        -CLI) cli=true; shift ;;
        --) shift; break ;;
        *) break ;;
    esac
done
if [ -z "$version" ]; then
    selected=$(read_setting "$root/launcher.ini" launcher linux)
    if [ "$check" = false ] && [ "$custom" = false ] && [ "$(read_setting "$root/launcher.ini" launcher linux_custom)" != true ]; then
        case "$selected" in ''|*[!0-9]*) ;; *)
            if [ -f "$root/release-info/linux-x86_64-$selected.json" ] || [ -f "$root/.updates/installed/linux-x86_64-$selected.json" ]; then
                engine=$selected
                if [ -f "$root/.updates/engine.ini" ]; then engine=$(read_setting "$root/.updates/engine.ini" updater linux); fi
                case "$engine" in ''|*[!0-9]*) echo 'Invalid recovery engine' >&2; exit 2 ;; esac
                LD_LIBRARY_PATH="$root/linux/$engine/lib" "$root/linux/$engine/bin/zima-cad-update" recover --root "$root"
            fi ;;
        esac
    fi
    version=$(read_setting "$root/launcher.ini" launcher linux)
    if [ "$custom" = false ]; then custom=$(read_setting "$root/launcher.ini" launcher linux_custom); fi
fi
case "$version" in ''|.|..|*[!a-zA-Z0-9_.-]*) echo 'Invalid selected version' >&2; exit 2 ;; esac
if [ "$custom" = true ]; then runtime="$root/custom/linux/$version"; else
    case "$version" in *[!0-9]*|*00) echo 'Invalid official build ID' >&2; exit 2 ;; esac
    [ "${#version}" -eq 10 ] || exit 2
    runtime="$root/linux/$version"
fi
[ "$(read_setting "$runtime/build.ini" build product)" = ZIMA-CAD ] || exit 2
[ "$(read_setting "$runtime/build.ini" build version)" = "$version" ] || exit 2
entry="$runtime/bin/zima-cad-cpp"
[ "$cli" = false ] || entry="$runtime/bin/zima-cad-cli"
[ -x "$entry" ] || exit 2
if [ "$check" = true ]; then printf '%s\n' "$entry"; exit 0; fi
mkdir -p "$root/config/windows" "$root/config/linux" "$root/config/templates" "$root/config/materials" \
    "$root/config/formats" "$root/config/localization" "$root/Projects" "$root/cache" "$root/autosave" "$root/recovery" "$root/.updates"
# Create only if absent, without truncating a user's configuration.
(set -C; : > "$root/config/config.ini") 2>/dev/null || [ -f "$root/config/config.ini" ]
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH
export LD_LIBRARY_PATH="$runtime/lib"
export QT_PLUGIN_PATH="$runtime/plugins"
export CSF_ShadersDirectory="$runtime/resources/occt/Shaders"
export CSF_XSMessage="$runtime/resources/occt/XSMessage"
export CSF_SHMessage="$runtime/resources/occt/SHMessage"
export CSF_XSTEPDefaults="$runtime/resources/occt/XSTEPResource"
cd "$root"
exec "$entry" "$@"
