# C++ performance measurements

The startup-independent C++ model benchmark is built as
`zima_cpp_performance_benchmark`. It is deliberately not registered with
CTest: normal tests remain deterministic and do not depend on machine speed.

Build and run it from an existing CMake build directory:

```bash
cmake --build build/cpp-debug --target zima_cpp_performance_benchmark
build/cpp-debug/zima_cpp_performance_benchmark
```

The command prints a `ZIMA_PERF_V1` contract followed by mean wall-clock
times over three repetitions. The fixture is fixed at 24 Part history
features, 256 Assembly components, and three nested Assembly levels.
Measurements cover:

* Part history calculation through the OCCT kernel;
* Assembly scene construction and ordered viewer picking;
* explicit regeneration of the complete open nested dependency chain.

Reported times are observations, not pass/fail thresholds. Compare runs on the
same machine and build configuration; compiler, OCCT, CPU, and thermal state
substantially affect them. The fixture checks result sizes while running, so a
regression in the measured operation fails independently of its duration.

## Python parity measurement

Do not compare the archived Python `build_active_shape()` time directly with
C++ `evaluate_history()`. The Python call returns only the final live OCCT
shape, while the C++ call also materializes persisted ZIMA viewer packets for
every history boundary. Run the matching Python fixture with:

```bash
runtime/linux/python/bin/python tools/benchmark-python-part.py
```

Representative Linux Release measurements for the same 24 overlapping boxes:

```text
Python shape only:                   238.658 ms
Python final viewer packet:          628.028 ms
Python all 24 boundary packets:     4436.083 ms
Python changed-final viewer packet:  407.774 ms

C++ all 24 boundary packets:         846.270 ms
C++ changed-final boundary packet:    56.574 ms
```

For the same persisted-boundary work, C++ is about 5.2 times faster for a full
history and 7.2 times faster for editing the final feature on this fixture.
Exact ratios remain observational and must be remeasured on the same machine.

The native benchmark additionally covers live topology ancestry. A 24-feature
history followed by Fillet was measured at roughly 36-39 ms when editing the
Fillet with the live boundary cache, versus roughly 938-971 ms through a cold
kernel that must reconstruct ancestry from the beginning. The live cache owns
the OCCT shape and shared persisted-identity maps for faces, edges and vertices;
the maps are not serialized into the document and are not copied for every
cached boundary.

Example output from the bundled Linux debug build (milliseconds):

```text
ZIMA_PERF_V1 repetitions=3
part_history features=24 boundaries=24 mean_ms=1533.297
assembly_scene components=256 triangles=9216 mean_ms=23.015
assembly_picking components=256 candidates=25 mean_ms=4.834
nested_regeneration levels=3 mean_ms=38.082
```

## Build and startup measurements

These complement the model-level benchmark above with the remaining
acceptance-gate observations: build time and full-application startup
behavior. Same caveats apply — these are observations on one machine and
build configuration, not pass/fail thresholds.

Measured on the same Linux Debug/Ninja build directory (`build/cpp-debug`,
24 logical CPUs, `-j$(nproc)`):

* Clean build (`ninja -t clean` then full `ninja`, 86 build steps): ~37 s.
* Incremental build after touching one already-compiled `.cpp` (`main_workspace.cpp`,
  recompile + relink only that translation unit and its executable): ~5.2 s.
* Full `--verify-startup` run (headless `QT_QPA_PLATFORM=wayland`, exercises
  every scripted GUI regression scenario end-to-end — Part/Assembly/Drawing
  creation, edit/cancel/undo/redo, nested and multi-level Assembly occurrence
  activation, RMB selection cycling, file-management rename/cleanup dialogs,
  and Deep Assembly mate creation/DOF/drag): ~7.6 s average over 3 runs
  (7.61-7.69 s), i.e. real end-to-end regression coverage completes well
  under 10 seconds, not just a bare window open.
