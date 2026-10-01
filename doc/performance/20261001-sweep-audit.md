# 2D, 3D and Helical Sweep follow-up audit

## Scope and checks

The user requested the same review for all three sweep commands: calculation,
validation, reference preparation and unchanged Properties confirmation, without
reducing accuracy. The preceding seam-side optimization already applies to all
three because they share result-packet preparation. The global transported-path
self-intersection pass was specific to Helical Sweep. The ordinary 2D/3D path
has no equivalent global pass to remove; its pipe, loft, offset-profile and
Boolean validity checks remain in place.

Inputs → means → outputs: authored sweep paths/profiles → existing explicit
geometry calculations and reference packets → equivalent body geometry,
properties and reference identities with redundant work removed. Benchmarks use
an exact planar circular path and a rounded spatial path at identical 0.1 mm
construction tolerance, 0.001 mm Boolean tolerance and 0.1 mm mesh deflection.
The Helical no-op check uses a copy of the user-provided `Projects/02.prtz`.

## Changes

Two additional mesh-only consumers now omit aggregate volume/area/inertia
integration: grouped child reference packets and original endpoint-cap meshes.
Only their mesh was consumed. Per-face area, edge length, exact reference geometry
and final/retained Body properties remain calculated. All precision settings,
side identities, geometry validation and persistent data contracts are unchanged.

`commit_sweep` now checks its existing unchanged/origin-display-only path before
copying the complete Part document. Authorization, ownership, input validation
and value-lock checks retain their order. The shared origin-display/placement
contract is consumed unchanged. Regression tests cover repeated unchanged
confirmation for 2D/3D/H, stable revision/data generation/calculated storage,
Undo state and preserving an existing Redo branch.

## Calculation measurements

Six fresh-kernel measured samples per case after two warm-up samples; serial
Windows Release runs, no overlapping tests or compilation. This is the existing
precision benchmark with both requested tolerances deliberately identical.

| Fixture | Before mean | After mean |
| --- | ---: | ---: |
| Planar circular 2D Sweep | 175.951 ms | 177.118 ms |
| Rounded spatial 3D Sweep | 460.039 ms | 459.700 ms |

These measurements do not establish a meaningful overall speedup on these two
small examples. The accepted change removes proven discarded work; no large
benefit is claimed. Volumes, areas, triangle counts, path segment counts and BRep
validity match exactly across corresponding samples. Raw reports are stored in
`20261001-sweep-audit-before.json` and `20261001-sweep-audit-after.json`.

Recalculating the saved baseline fixtures with the changed code preserves all
calculated cache/native geometry and reference data. The complete 2D file is byte
identical. The 3D file differs only in the embedded profile Sketch's derived
`plane_reference_owner_id`, filled by the existing load-time frame resolution;
no calculated packet or physical property differs. This fixture normalization is
not a production format or algorithm change.

## Properties measurements

A repeatable GUI probe is available through `ZIMA_VERIFY_SWEEP_NOOP_DOCUMENT`
combined with `ZIMA_VERIFY_HELICAL_SWEEP_ONLY=1` and `--verify-startup`. It copies
the input into the verification directory, selects its first 2D/3D/H feature,
opens the actual Properties dialog, presses OK without edits three times, and
asserts unchanged document state and Undo/Redo availability. No user input file
is saved over. Initial investigation temporarily installed throwing tripwires at
all three OCCT history evaluation entry points after loading; all three command
probes completed normally. These diagnostic kernel hooks were removed.

| Input | Warm Properties open | Warm unchanged OK |
| --- | ---: | ---: |
| Circular 2D fixture | 50–52 ms | 24–25 ms |
| Rounded 3D fixture | 62–64 ms | 9–11 ms |
| User `02.prtz`, before minor document-copy removal | 461–475 ms | 283–287 ms |

First-use UI overhead is higher (2D first OK about 99 ms). These timings include
processing pending UI events, not document loading or saving. No timing threshold
is asserted by the regression test; it tests state instead.

The precision benchmark now saves native fixtures for this GUI/recalculation
check and supports the `planar-spatial` filter. Its circular 2D path fixture now
sets its owning container explicitly; this was required by native validation and
does not alter the path geometry. Product UI text and translation keys are
unchanged. No precision, native schema or shared placement edits were made.

## Final verification

All ten selected Windows contracts passed in 115.26 seconds: 2D/H GUI, core
geometry, 2D/3D/H model contracts, Surface/Thin profiles, Sweep commands and
five-language catalogs. After adding the explicit retained-Redo assertion, the
Sweep command contract was rebuilt and passed again in 17.81 seconds.
All three actual GUI no-op probes passed on the final application. The final
Helical probe overlapped test/build activity and is retained as functional
verification only, not as a controlled speed comparison.

Logs: `build/sweep-audit-final-build.log`, `build/sweep-audit-regression.log`,
`build/sweep-noop-redo-tests.log` and `build/*-noop-final.log`.
GUI/CLI were rebuilt, the development launcher is unchanged, and no portable
release or Linux verification was performed. Localization review found no changed
product UI text; the existing five-language validation passed.
