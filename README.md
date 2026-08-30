# ZIMA-CAD

ZIMA-CAD is developed as a C++/Qt application with OCCT used only for explicit
solid-model calculations. The active implementation lives in [`cpp/`](cpp/).

## Build and run

```bash
cmake --preset linux-runtime-debug -S cpp
cmake --build build/cpp-debug --target zima-cad-cpp
./zima-cad -w Projects
```

The root launchers `zima-cad` and `zima-cad.bat` start the C++ application.
They prefer a Release build and fall back to the Debug build.

The planned release form is one self-contained portable Linux and Windows
archive with its complete runtime, resources, directory layout and portable
settings. The distribution contract is documented in
[`doc/PORTABLE_RELEASE.md`](doc/PORTABLE_RELEASE.md).

## Frozen Python reference

The former Python implementation is frozen in [`archive/python/`](archive/python/).
It is retained only as a runnable behavioural and visual reference; new
features and fixes belong exclusively to C++.

```bash
./archive/python/run.sh -w Projects
```

Shared configuration, resources, project data and the bundled runtime remain
at repository root and are supplied to the archived application by its
launcher. Historical Python documentation is preserved as
[`archive/python/README.md`](archive/python/README.md).

## Verification

```bash
cmake --build build/cpp-debug
ctest --test-dir build/cpp-debug --output-on-failure
```

Architecture and migration notes are in [`doc/`](doc/).
