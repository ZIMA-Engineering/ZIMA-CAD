# Remaining-area investigation — 2026-10-01

## Inputs, means and result

The user authorized review of the outstanding View, Assembly Drawing, repeated
measurement and STEP/IGES performance hypotheses, followed by a Windows release.
Existing calculated packets and actual local import files are the inputs. Source
tracing, bounded read-only probes, exact output comparisons and focused contracts
are the means. The required output is measured improvement without lost behavior,
plus an explicit disposition where a candidate has not met that bar.

## View

Accepted: [end-plane border preparation](20261001-end-plane-scene.md). This removes
repeated edge scans using the existing local map; it does not introduce persistent
GPU state or change rendering quality. A full GPU/scene redesign remains outside
the proven change. The focused viewer, selection, Sweep and Assembly tests pass.

## Assembly Drawings

Traced `DrawingProjection::Impl::get`, its live stamps and captured native reads.
Unchanged nested Assemblies already reuse sources and interactive cameras. The
existing regression also changes a nested Part while retaining its file timestamp
and requires fresh geometry. Narrowing the all-Workspace gate to just the root
Assembly would miss live nested-source changes; narrowing it only to visible
meshes would omit BOM, annotations, Family roots and section data. No partial
invalidation rule was substituted. Drawing view/dependency tests passed. A complete
per-source dependency capture remains unresolved, rather than claimed optimized.

## Repeated measurement

Traced the dialog resolver, authoritative occurrence scene, `resolve_measurement`
and the Save transaction. Occurrence area/volume/mass and Part material density
are resolved alongside displayed geometry. Save deliberately evaluates fresh
references after checking runtime identity and document generation. Caching only
by reference ID or viewer revision could keep stale physical metadata. The three
accepted operation-local improvements remain in place; no cross-operation cache
was introduced without a complete invalidation contract. Measurement core,
commands, edits, inspector and drawing-dimension contracts passed.

## STEP/IGES: measured local files

The native `zima_cpp_import_model_contract_tests --profile-file <path>` probe
imports into a disposable in-memory Part, validates Body ownership and finite
physical properties, and writes no model or input file. A Python supervisor
verified input SHA-256 before and after and bounded each owned probe at 180 s.
These are single diagnostic observations, not before/after speedup claims.

| Local input | Bytes | Observation |
| --- | ---: | --- |
| `8073895_DGST-16-10-L-PA.stp` | 674,919 | Separate structure inspection 220.254 ms; complete Part import 3,091.640 ms, 6 Bodies, 19,598 final triangles |
| `ze0026-0000-0000.stp` | 9,092,072 | Separate inspection 2,581.772 ms, 508 nodes; complete import exceeded the 180 s probe limit |
| `ze0026-0000-0000.igs` | 37,994,290 | Complete import exceeded the 180 s probe limit |

A timeout is not evidence of corrupt input or permanent nontermination. Only the
owned diagnostic process was terminated; no user CAD process was affected.
Sources remain unchanged and are not committed or packaged.

The STEP structure reader and kernel component transfer each load the file, but
the 2.6 s inspection alone cannot explain a greater-than-three-minute import.
Component transfer already shares its XCAF document within one batch. Topology
capture, reference persistence, mesh preparation and subsequent native history
construction remain required work and the next profiling boundary.

Two bounded experiments were rejected: indexing the `IsPartner` lookup and
reusing transferred source-entity records within one batch. The first preserved
all six complete B-Rep/viewer/binding packet hashes in eight alternating small-file
runs, but median capture time was 1,481.435 ms before and 1,494.920 ms after. The
large full import still exceeded 180 s. The second also matched the small-file
packets but did not establish a reliable performance benefit; its timing run
overlapped another diagnostic and is not accepted timing evidence. Both production
experiments were reverted. The isolated large baseline component capture also
exceeded its 90 s diagnostic limit. There is no STEP/IGES speedup claim for this
release, and the large-file bottleneck is explicitly unresolved.

`zima_cpp_step_model_contract_tests --capture-file <path>` retains a deterministic
read-only probe with fixed temporary owners and complete serialized body hashes,
so a later candidate can be compared without relaxing topology identity or shape
validation. Ordinary fixture tests still cover units, repeated definitions,
placements, curved topology, frozen save/reopen and first-position imported Bodies.

## Disposition

The review and safe View change are complete; the broader cache redesigns and
large import bottleneck remain open. No functional validation or accuracy was
removed, and no unproven dead-code cleanup was committed. Linux stays on its
assigned host. All product text is unchanged; five-language catalogs passed.
English documentation and test-only diagnostics are included in this change.

Evidence: [read-only import observations](20261001-remaining-investigation.txt).
