# Windows runtime and release packaging

The product-level portable distribution layout and settings contract are
defined in [`PORTABLE_RELEASE.md`](PORTABLE_RELEASE.md). The preferred release
is one self-contained archive carrying both the Linux and Windows launchers,
runtimes and shared portable data.

The active application is now the C++ `zima-cad-cpp` target. A supported C++
Windows portable-build pipeline has not yet been established.

Do not publish a Windows build by adapting or bypassing the former Python
packaging scripts. Before the first C++ Windows release, add a repository-owned
build and validation pipeline that verifies at least:

- the exact committed Git source used for the build;
- the C++ executable and all Qt/OCCT runtime DLL dependencies;
- archive path safety, collisions, CRC and SHA-256;
- extraction and GUI startup from a normal Windows user directory;
- a deterministic Part calculation smoke test.

The frozen Python/OpenBLAS packaging procedure and its scripts are retained
only for historical reference under `archive/python/`.

## Console-free desktop start

The `zima-cad-cpp` target uses the Windows GUI subsystem. Qt supplies its
Windows entry point through `Qt6::Core`. CLI verification still accepts
`--verify-startup` with redirected output.

After a local build, `tools/create-windows-shortcut.ps1` creates or refreshes
`zima-cad.lnk` on the Desktop. It points directly to the selected executable,
sets the repository working directory and passes `--working-directory`.
There is no console or script-host process in the ordinary desktop launch.
The root `.bat` remains an asynchronous convenience for command-line users.
