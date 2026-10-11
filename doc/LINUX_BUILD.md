# Native Debian build and release

The supported target is Debian 13 (trixie), x86_64, with KDE/Wayland.
Other distributions are not part of this acceptance target. C++ is the only
application runtime; Python is used only by developer packaging tools.

## Native continuation on 2026-10-11

The Windows handoff at `5947de4b37d29d768a3c2bb4d98f3acbe703ba99` was received
and built on Debian 13.6 x86_64, GCC 14.2.0, CMake 3.31.6 and Qt 6.8.2.
The OCCT 8.0.0 SDK was rebuilt with the shared Gauss patch SHA-256
`2f500bc8e2e0346be3c6770351c3ac043d8b1a9da4a904aa4e829f3af45e6863`.
GUI, CLI, Drawing harness, updater and all native test targets build.
The repository launcher continues to select `build/cpp-native-release`.

Linux exposed FORM reuse rejecting a verified Windows surface because its
nested Body runtime key differed. The fix consumes the current complete
definition/history proof while retaining local-key validation and changed-source
rejection; see [the calculation protocol](CALCULATED_MODEL_PARITY.md).
No new UI text, format fields, factory asset changes or protected placement
changes are needed. All five localization catalogs pass.

The final focused native run passes 35 suites. The native contact matrices
pass 3280 arc centre/end/axis cases, 108 tangent cases and the finite C+K matrix.
All eleven retained corner lifecycle/range tests and both Window copy suites
pass. The current finished outward corner passes nine thickness/radius cases,
independently measured Fillet radii and both directed differences against the
separate Cut/Thickness/quarter-cylinder Trim/Fuse construction. Window passes
both outer sides at 1/2/3 mm with 37-degree rotation, unchanged OK, edits,
Undo/Redo, Unbend, save/reopen and regeneration. Both outward and inward
controls reject 4 mm with the same valid-solid thickening error.

Actual Wayland GUI passes the 48-case arc contact matrix (504.842 s), the six
tangent fixtures (68.294 s), C+K, file-dialog heartbeat/themes, Tree branches,
curve/point/axis presentation, import interaction, placement, treatment selection,
edge treatments, new templates, history deletion, application tools, dimension
entry, common tangent, Window and finished corner lifecycle. The seventeen-case
GPU connectivity test passes exact GPU bytes and zero channel differences
after waiting for Wayland's initial window configuration. The initial failing
frame-size result remains in the evidence; no renderer behavior changed.

Remaining observed failures are explicit:

- Spatial Sweep regeneration still fails placement convergence after its
  independent nine-section geometry checks, matching the Windows handoff.
- The inward Window role-reordering geometry suite rejects exact source sample
  equality: 348 of 611 unique samples differ, with maximum bidirectional nearest
  distance `5.313698319447236e-7` mm. It remains failing; its precision checks
  were not relaxed. Both full copy matrices subsequently pass.
- The general GUI suite cannot warp the cursor on Wayland; XWayland passes
  that stage but fails the selected-wire thickness assertion. This is not a
  complete general GUI acceptance result.
- The extended Boundary fixture initially reopened its invented lower-case
  filename although the configured naming policy saved an uppercase file.
  The test now uses Save's actual returned path; desktop rerun is pending.

G92H1-T shared preparation completes in 67.1013 s and G92K1 in 912.692 s,
without a kernel crash. Their packet SHA-256 values are respectively
`19fc7576f6779c539d5ddf04de49c57a642f0eb2e5a5db55567d5f9f3c613f2a`
and `688c6bd9d10b3c5929d51e71c59ea35e78a511ed6ae21a6f8867f3c3d74daeae`.
These Linux observations overlap regression work; they are not controlled
Windows/Linux speed ratios or evidence resolving the known G92K1 integration
discrepancy. Full large-model desktop lifecycle, rotation and release acceptance
are still in progress.

Packaging tests pass 10 cases with one platform skip, publisher gates pass 16,
and isolated updater lifecycle passes all 23. Evidence is under
`build/linux-1106-*`, including per-suite logs and JSON results. The prefix
identifies the receiving build; the new release identity is 2026101107.
Signing, finalized archive smoke and publication remain separate gates.

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
