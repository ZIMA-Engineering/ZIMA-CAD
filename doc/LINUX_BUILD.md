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
OCCT 8.0.0 SDK (matching the Windows dependency pin), verified against its
source archive SHA-256:

```sh
./tools/distribution/build-linux-sdk.sh
cmake --preset linux-runtime-release -S cpp
cmake --build build/cpp-release --parallel 12
```

The retained preset names now select native dependencies under
`build/native-sdk/occt-8.0.0`, with Debian Qt and OpenSSL. No Conda location is used.
An explicit `-DCMAKE_PREFIX_PATH=/path/to/native/sdk` remains supported.
The SDK disables Draw, visualization and optional Tcl/Tk/TBB/FreeType support;
the application uses OCCT as a geometry kernel and its own Qt/OpenGL viewer.
SDK source, compiled dependencies and logs remain ignored development artifacts.
The version-specific SDK/build directories preserve the previous SDK while the
new kernel is verified. Both platforms use the same modeling code and OCCT
source version; platform integration and packaging remain platform specific.

## Candidate builder

Commit the intended changes and choose a new `VERSION`. Use an unused short
staging path and output directory:

```sh
python3 tools/distribution/build-linux.py \
  --sdk "$PWD/build/native-sdk/occt-8.0.0" \
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

## OCCT alignment verification on 2026-10-06

The native Linux SDK now uses OCCT 8.0.0, matching the Windows vcpkg baseline
`e03dc9b29710050cd1018bc5674688108658d327`. The downloaded V8_0_0 source archive
SHA-512 matched that baseline's opencascade port. Its pinned SHA-256 is
`118398ff8a010c2cb693450d7e5e2690533c88208fc25bd2730451ec4fab0a0f`.
No platform-dependent surface-intersection algorithm or OCCT 7.x API branch
remains. The CMake minimum is 8.0. Presets explicitly select both the SDK prefix
and its OpenCASCADE config directory, overriding a cached 7.9 package location.

SDK build/install and native GUI/CLI/updater builds passed. The root development
launcher selects the rebuilt GUI; GUI/CLI build metadata reports OCCT 8.0.0 and
linked kernel libraries resolve into `build/native-sdk/occt-8.0.0`.
There are no new or changed application UI strings. Localization coverage and
catalog/placeholder validation passed for all five languages.

Ten of twelve selected native suites passed: translations, Thicken Surface,
Boundary Surface/intersections, Revolution limits, edge treatments, Shell,
multiple Bodies, interchange, STEP model and sheet transitions. Two suites fail
identically on the preserved 7.9.3 baseline: Extrusion limits expects 20 but gets
20.635083, and Bend's expanding-profile-axis clearance fixture rejects an invalid
Assembly cut. These existing failures remain outside the kernel-alignment fix.
The baseline executables were verified to link OCCT 7.9 libraries. No full-suite
or cross-platform result-equivalence claim is made.

Actual KDE/Wayland Intersection and Trim GUI scenarios passed. A disposable
relocated native runtime with version-local OCCT/Qt libraries and clean factory
configuration passed the standard launcher/CLI pipes, 6000 mm3 calculation,
native save/reopen, PDF/JPEG, rendered GUI and configuration-preservation smoke.
General Surface and template GUI scenarios also passed there, including five UI
languages. With the user's development configuration, the General Surface test
hits a viewport-height assertion and the template test's timer crashes in
QLineEdit::setText; these configuration-dependent GUI-test limits are recorded,
not reported as passing or attributed to the kernel without evidence.

Evidence: `build/linux-occt8-*.log`; disposable runtime under
`/tmp/zima-occt8-isolated/`. This is local development validation, not a newly
published release. The immutable public 2026100601 archive still contains
OCCT 7.9.3. Windows was not rebuilt or executed on this Linux host.
