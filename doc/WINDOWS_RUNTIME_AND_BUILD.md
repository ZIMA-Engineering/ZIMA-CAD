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
Run `tools/register-windows-file-types.ps1` after building to associate the
current document/template extensions with this EXE for the current user.
External launches create separate processes; [instance numbering and project
isolation](MULTIPLE_INSTANCES.md) also apply to Window > New Window.
The root `.bat` remains an asynchronous convenience for command-line users.


## Local JPEG export dependency

Local CMake builds deploy `Qt6::QJpegPlugin` into `imageformats` next to the
application and drawing harness. `tools/deploy-qt-image-plugin.cmake` resolves
and copies its non-system DLL dependencies, including the JPEG codec, from
the configured Qt runtime. The installed Windows/MSVC runtime remains the
same prerequisite as for the existing local C++ executable. The drawing UI
contract writes and decodes a JPG without an external Qt plugin search path.
This is local development deployment, not a portable-release pipeline.


## Local command-line executable

`tools/build-windows.ps1` builds both `zima-cad-cpp.exe` and `zima-cad-cli.exe`.
The CLI uses the console subsystem, a Unicode Windows entry point, UTF-8
command input/output and the common command host. It does not link Qt or
create a GUI. OCCT and the C++ runtime remain required. The CLI target is
included in the CMake install target; this does not establish a portable
release pipeline. See [CAD_COMMAND_LINE.md](CAD_COMMAND_LINE.md).
