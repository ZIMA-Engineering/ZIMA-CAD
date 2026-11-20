# Initial native FEM mesher verification

The isolated Gmsh integration passed seven synthetic geometry cases on Windows.
This is the first implementation checkpoint for the [FEM design](../FEM_DESIGN.md),
after Windows 2026112004 publication. It adds no FEM command to that release and
does not change native document storage or the user's development executable.

## Build and source

Baseline: `2adfc6ae9`. Gmsh 4.15.2 was built with MSVC 19.51.36248 in Release
against the existing product OCCT 8.0.1 SDK and its Gauss integration patch.
The [machine record](20261120-fem-mesher.json) contains source, patch, recipe,
binary and probe hashes and the complete observed metrics.

The upstream source required C++17, relocated OCCT iterator headers, explicit
formerly transitive includes, the current 2D curve-properties name and Handle
syntax. The [shared dependency patch](../../tools/dependencies/gmsh-occt8.patch)
contains these API/build adaptations without changing modeling or meshing
algorithms. The probe does not use a prebuilt Gmsh SDK with a different kernel.

The source preparation tool accepted the pinned original archive, validated its
4,920 entries and applied the reviewed patch in a new directory. The shared
CMake profile rebuilt all 268 required compilation/link steps. An initial profile
enabled unwanted optional modules because upstream resets its INTERNAL default;
that build was stopped and the profile now pins individual options explicitly.
The completed minimal profile and its resulting library passed the same tests.

## Geometry checks

All cases require a valid solid source, a unique exact Gmsh binding for every
native face and nonempty boundary elements on every bound face. Import uses a
private deep copy. The source BRep serialization remains identical after each
mesh generation. No nearest-face or bounding-box lookup supplies these bindings.

| Synthetic fixture | Order | Nodes | Tetrahedra | Bound faces | Relative volume error |
| --- | --- | ---: | ---: | ---: | ---: |
| Box 20 by 10 by 5 | Linear | 130 | 340 | 6 | 3.41e-12 |
| Plate with through hole, size 1.25 | Quadratic | 4,127 | 1,968 | 7 | 2.25e-6 |
| Same plate, size 0.8 | Quadratic | 12,590 | 6,808 | 7 | 5.62e-7 |
| Small fillets | Quadratic | 4,315 | 2,397 | 26 | 1.09e-4 |
| Thin plate, size 2.0 | Quadratic | 467 | 206 | 6 | 3.40e-12 |
| Same thin plate, size 0.45 | Quadratic | 8,514 | 4,582 | 6 | 3.38e-12 |
| Solid 90-degree twisted strip | Quadratic | 9,541 | 4,728 | 6 | 1.49e-9 |

Volume is integrated from tetrahedron Jacobians using Gauss4 quadrature and
compared with OCCT volume properties. Every sampled determinant is positive and
finite. This is not a proof of Jacobian positivity everywhere in a curved element.
The largest observed relative volume error is approximately 0.0109 percent.

The refined 1 mm plate's maximum element span across thickness is 0.623 mm,
versus 1 mm in the coarse mesh. The check rejects any refined element spanning
0.75 mm or more. This proves actual through-thickness subdivision in this
fixture, not mechanical bending convergence or an automatic sheet refinement
policy. The holed plate is meshed twice from the same unchanged native source.

Per-case observations range from approximately 0.009 to 0.351 seconds on this
host, including copying, import, bindings, meshing and checks. These small
synthetic timings are not promised performance for user models.

## Localization and unchanged product

No product UI text, menu or dialog is changed. A read-only catalog check using
the existing INI-plus-JSON merge rules passed for Czech, English, German, French
and Russian: 4,648 Qt keys, 938 named keys, preserved placeholders and 4,284
production translation call sites covered. GUI language switching was not
repeated for this non-UI checkpoint. Build-tool and test diagnostics are English.

The published Windows package, local launcher, native format, templates,
factory library and protected project originals are unchanged by this probe.
The native FEM storage proposal is awaiting its scoped agreement.

## Remaining acceptance

Linux build/execution, real ZIMA Body and persisted region-key binding, partial
projected regions, direction/side handling, conditions and consistent load
integration remain unverified. The numerical solver, stress/displacement
results, convergence, application UI, cancellation and native FEM persistence
are subsequent implementation. No stress or deformation claim follows from
these geometry tests.

Reproduction instructions and the standalone C++ probe are in
[`tools/verification/fem_gmsh`](../../tools/verification/fem_gmsh/README.md).
