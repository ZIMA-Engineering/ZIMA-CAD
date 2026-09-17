# ZIMA-CAD C++ implementation

This directory contains the only active product implementation. The former
Python application and runtime were removed with the user's approval on
2026-09-15. Historical source remains in Git; it is not a runnable dependency or
behavioral authority for current development.

## Architecture and source navigation

- [Native architecture](../doc/CXX_ARCHITECTURE.md): modules, calculation boundary,
  persisted references, history, performance and deferred design work.
- [Native behavior](../doc/NATIVE_BEHAVIOR_CONTRACT.md): ownership, interaction,
  transactions and verification.
- [Workspace source map](../doc/WORKSPACE_SOURCE_MAP.md): application file ownership
  and build registration under `app/workspace`.
- [Command coverage](../doc/CAD_COMMAND_COVERAGE.md): current GUI/CLI operations.
- [CAD console](../doc/CAD_CONSOLE.md) and [CLI](../doc/CAD_COMMAND_LINE.md): usage,
  typed commands, scripts and host boundaries.
- [User manual](../doc/UZIVATELSKY_MANUAL.md): current modeling workflows.

`zima-cad-cpp` is the desktop application and `zima-cad-cli` is the command-line
application. They share model operations. Part and Drawing harness targets are
developer verification programs, not separate products.

The former chronological first-slice notes have been replaced by these maintained
contracts. Their original progress records remain in Git history.

## Windows build

From the repository root:

```powershell
./tools/build-windows.ps1 -Configuration Release
```

The script finds Visual Studio, its vcpkg, CMake and Ninja. The manifest pins
native dependencies; downloads, installed packages and build outputs stay under
`build/`. Add `-RunTests` for CTest or use `-Configuration Debug` for Debug.
The Release executables are `build/cpp-windows-release/zima-cad-cpp.exe` and
`build/cpp-windows-release/zima-cad-cli.exe`. `VCPKG_ROOT` is optional when using
a standalone vcpkg checkout instead of Visual Studio's component.

See [Windows runtime and build](../doc/WINDOWS_RUNTIME_AND_BUILD.md) for deployment
plugins and local launchers. A successful local build is not a portable release.

## Linux build

Use Debian 13 x86_64 with native Qt development packages and the pinned OCCT
7.9.3 SDK. See [Native Debian build and release](../doc/LINUX_BUILD.md).

```sh
./tools/distribution/build-linux-sdk.sh
cmake --preset linux-runtime-release -S cpp
cmake --build build/cpp-release --parallel 12
```

The retained preset names now use `build/native-sdk/occt`, without Conda.
CMake and Ninja remain build prerequisites, not application runtime components.

## Packaging

[The distribution policy](../doc/DISTRIBUTION_CLEANUP_PLAN.md) defines build IDs,
version directories, complete native dependencies, exact source export, user-data
separation and validation gates. Native candidate builders, launchers and signing
are described in [Native distribution](../doc/NATIVE_DISTRIBUTION.md). Keep all project documentation in English.
