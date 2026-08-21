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

Example output from the bundled Linux debug build (milliseconds):

```text
ZIMA_PERF_V1 repetitions=3
part_history features=24 boundaries=24 mean_ms=1533.297
assembly_scene components=256 triangles=9216 mean_ms=23.015
assembly_picking components=256 candidates=25 mean_ms=4.834
nested_regeneration levels=3 mean_ms=38.082
```
