# Windows runtime and release packaging

The product-level portable distribution layout and settings contract are
defined in [`PORTABLE_RELEASE.md`](PORTABLE_RELEASE.md). The preferred release
is a self-contained archive carrying the launchers, available platform runtimes
and shared portable data. Platforms may publish independently; published archives
remain immutable.

The active application is now the C++ `zima-cad-cpp` target. A supported C++
Windows candidate pipeline is implemented in `tools/distribution/`; see
[NATIVE_DISTRIBUTION.md](NATIVE_DISTRIBUTION.md) for commands, configuration
ownership and the distinction between a candidate and a signed official release.

Do not publish a Windows build by adapting or bypassing the former Python
packaging scripts. The repository-owned build and validation pipeline must verify at least:

- the exact committed Git source used for the build;
- the C++ executable and all Qt/OCCT runtime DLL dependencies;
- archive path safety, collisions, CRC and SHA-256;
- extraction and GUI startup from a normal Windows user directory;
- a deterministic Part calculation smoke test.

The Python application and its Python-only packaging scripts were removed on
2026-09-15. Their source is available in Git history. Conda and Python are not
C++ runtime requirements. The current Windows vcpkg manifest selects Qt,
OpenCASCADE, FreeType, HarfBuzz, OpenSSL and nlohmann_json. Package the actual native
runtime dependencies and dynamically loaded Qt plugins; a header-only library
does not require a runtime DLL. The CLI also needs its offscreen Qt platform.

The old runtime was removed at the user's request after a fresh Windows GUI/CLI
build and console contract passed with it detached. Linux development presets
still need a new native dependency setup on Linux; they cannot use the removed
Conda path. Version layout and release gates are in [DISTRIBUTION_CLEANUP_PLAN.md](DISTRIBUTION_CLEANUP_PLAN.md).

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
isolation](MULTIPLE_INSTANCES.md) apply to every external launch. The Window menu
only switches documents within the current instance.
The user's normal development entry point is `zima-cad.bat` in the repository
root, confirmed on 2026-09-15. Its name and location remain stable. It starts
the current local C++ executable asynchronously with the repository working
directory; no build-directory navigation is required after each rebuild.
Keep it working when build paths change. Portable releases retain their own
installation-root launcher and do not replace this development entry point.


## Local JPEG export dependency

Local CMake builds deploy `Qt6::QJpegPlugin` into `imageformats` next to the
application, drawing harness and command-line executable. `tools/deploy-qt-image-plugin.cmake` resolves
and copies its non-system DLL dependencies, including the JPEG codec, from
the configured Qt runtime. The installed Windows/MSVC runtime remains the
same prerequisite as for the existing local C++ executable. The drawing UI
contract writes and decodes a JPG without an external Qt plugin search path.
The CLI process test also exports PNG/JPEG through its offscreen graphics
runtime; the native image command tests decode both formats and verify DPI.
This is local development deployment, not a portable-release pipeline.


## Local command-line executable

`tools/build-windows.ps1` builds both `zima-cad-cpp.exe` and `zima-cad-cli.exe`.
The CLI uses the console subsystem, a Unicode Windows entry point, UTF-8
command input/output and the common command host. Shared PDF export uses
Qt Gui/Svg and a QGuiApplication with the offscreen platform; it creates no
QWidget or main window. CMake deploys Qt6::QOffscreenIntegrationPlugin into
`platforms` beside the CLI executable. Qt, OCCT and the C++ runtime remain
required. The CLI target is
included in the CMake install target; this does not establish a portable
release pipeline. See [CAD_COMMAND_LINE.md](CAD_COMMAND_LINE.md).
