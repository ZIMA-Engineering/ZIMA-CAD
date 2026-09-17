# Native Debian build and release

The supported target is Debian 13 (trixie), x86_64, with KDE/Wayland.
Other distributions are not part of this acceptance target. C++ is the only
application runtime; Python is used only by developer packaging tools.

## Dependencies

Install the Debian build dependencies:

```sh
sudo apt-get install build-essential cmake ninja-build curl patchelf \
  qt6-base-dev qt6-base-private-dev qt6-svg-dev qt6-wayland \
  qt6-svg-plugins qt6-image-formats-plugins libssl-dev libharfbuzz-dev \
  libfreetype-dev nlohmann-json3-dev libxkbcommon-dev python3-cryptography
```

Debian 13's OCCT 7.8 does not meet the application requirement. Build the pinned
OCCT 7.9.3 SDK, verified against its source archive SHA-256:

```sh
./tools/distribution/build-linux-sdk.sh
cmake --preset linux-runtime-release -S cpp
cmake --build build/cpp-release --parallel 12
```

The retained preset names now select native dependencies under
`build/native-sdk/occt`, with Debian Qt and OpenSSL. No Conda location is used.
An explicit `-DCMAKE_PREFIX_PATH=/path/to/native/sdk` remains supported.
The SDK disables Draw, visualization and optional Tcl/Tk/TBB/FreeType support;
the application uses OCCT as a geometry kernel and its own Qt/OpenGL viewer.
SDK source, compiled dependencies and logs remain ignored development artifacts.

## Candidate builder

Commit the intended changes and choose a new `VERSION`. Use an unused short
staging path and output directory:

```sh
python3 tools/distribution/build-linux.py \
  --sdk "$PWD/build/native-sdk/occt" \
  --stage /tmp/zima-linux-candidate --output .dist-output/linux-candidate
```

Only Git objects at `--commit` (HEAD by default) are exported and compiled.
`--repo` may point at a separate clean checkout for release gating. `--release`
requires clean input and the exact `ZIMA-CAD-<VERSION>` tag. `--reuse-build`
validates the prior staged source before reusing compilation, and always assembles
and validates a new package. Existing archives are never overwritten.

The builder bundles Qt, the pinned OCCT, OpenSSL, image/TLS/Wayland/X11 plugins
and their recursively resolved dependencies. Debian's libc and graphics
dispatchers/drivers remain host supplied. ELF RPATHs point at the version-local
`lib/`, and dependency closure is checked again after relocation. Metadata records
Debian package versions, compiler, libc, source commit, Qt/OCCT versions, the SDK
archive identity and the specific host-library names. Dependency license notices
accompany the runtime.

Archive validation checks member paths, case collisions, CRC and SHA-256.
Extraction restores executable modes from the validated inventory and uses a
path containing spaces and Unicode. With developer paths removed, the smoke
runs the root launcher and CLI pipes, computes a 10 x 20 x 30 mm Box, saves and
reopens it, checks its 6000 mm3 volume, exports PDF/JPEG, runs the rendered GUI
startup check and verifies that shared user configuration remains unchanged.

Run packaging and updater contracts before final acceptance:

```sh
python3 tools/distribution/test_package.py
python3 tools/distribution/test_update_release.py
ZIMA_UPDATER_TEST_EXE="$PWD/build/cpp-release/zima_update_test_helper" \
ZIMA_UPDATE_FIXTURE_EXE="$PWD/build/cpp-release/zima_update_fixture" \
  python3 cpp/tests/test_updates.py
```

The signed finalizer in [Application updates](UPDATES.md) repeats the Linux smoke
on the finalized signed archive. The publisher key must match the existing
trusted public key; it is never copied into source or the runtime. Unsigned
candidates are not authenticated official updates. Publish only after actual
native and desktop acceptance, and record the exact results in release notes.
