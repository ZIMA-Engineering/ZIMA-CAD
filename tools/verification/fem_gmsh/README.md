# Native FEM mesher probe

This standalone C++ probe tests Gmsh 4.15.2 against the product's patched OCCT
8.0.1. It does not add FEM commands or dependencies to the released application.
Windows results are in
[the finite verification record](../../../doc/verification/20261120-fem-mesher.json).
Linux execution remains required before cross-platform acceptance.

Download the [official source archive](https://gmsh.info/src/gmsh-4.15.2-source.tgz),
then run `tools/dependencies/prepare-gmsh.py --archive <archive> --root <new-root>`.
The tool checks pinned source and patch hashes, archive paths/collisions and
extraction bounds before applying the shared dependency patch. Use Python 3.12+
and Git for preparation; neither is a product runtime dependency.

Configure the prepared source with CMake and Ninja, using the shared initial
cache `tools/dependencies/gmsh-probe.cmake`:

```text
cmake -S <prepared-source> -B <gmsh-build> -G Ninja
  -C <repository>/tools/dependencies/gmsh-probe.cmake
  -DCMAKE_PREFIX_PATH=<occt-sdk> -DOCC_INC=<occt-sdk>/include/opencascade
cmake --build <gmsh-build> --target shared --parallel 4
```

Commands above are shown across lines for readability; pass each invocation as
one command or use the host shell's continuation syntax. On Windows run in the
MSVC developer environment. If CMake misdetects the compiler's localized include
prefix, pass its actual verified UTF-8 text as `ZIMA_MSVC_SHOWINCLUDES_PREFIX`, as
in the ordinary Windows builder. Verify the generated Ninja prefix. Use a
separate build directory; do not overwrite `build/cpp-windows-release`.

Check that configuration reports OCCT **8.0.1**, using the intended SDK and its
`share/opencascade/zima-sdk.json` provenance. Do not combine a prebuilt Gmsh SDK
with another OCCT binary. The shared patch updates C++17, relocated OCCT headers,
the 2D curve-properties type and Handle spelling. It changes no meshing or
modeling algorithm. CAF is disabled in this probe, so its compilation paths are
not accepted by these tests.

Configure this probe with `OpenCASCADE_DIR=<sdk>/share/opencascade`,
`CMAKE_PREFIX_PATH=<sdk>`, `GMSH_INCLUDE_DIR=<prepared-source>/api` and
`GMSH_LIBRARY=<gmsh-build>/gmsh.dll.lib` on Windows (the corresponding shared
library on Linux). Build `zima-fem-mesher-probe` and make the selected SDK and
Gmsh runtime libraries available to that process. It emits one JSON line per
accepted fixture and returns nonzero on failure.

Fixtures cover a box, a holed plate at two mesh sizes, a filleted box, a thin
plate at two sizes and a solid 90-degree twisted strip. Each exact source face
must bind to a unique Gmsh face with boundary elements. The source BRep archive
must remain unchanged. Check volume by tetrahedral Gaussian integration and
reject nonpositive sampled Jacobians. Refined thin-plate elements must not span
the full thickness. Timings include private copying, import, binding, meshing
and checks; they are synthetic observations, not interactive performance targets.

These tests do not prove Jacobian positivity everywhere, stress accuracy,
condition integration, native persistence, UI behavior or the mapping of ZIMA's
persisted reference keys. Those remain subsequent gates in
[the FEM design](../../../doc/FEM_DESIGN.md).

Gmsh and its enabled contributed libraries retain their upstream licenses. A
future distribution must include the applicable notices and corresponding
source; this probe does not authorize omitting dependency source from releases.
