# Remaining performance hypotheses and regression maintenance

## Measurement (A14)

Follow-up: [operation-local primitive allocation](20261001-measurement-allocation.md)
measures and removes vector growth without introducing cross-operation caching.
The original observations below remain historical baseline evidence.

The existing distance implementation builds a spatial index for the measured
geometry. Current callers calculate on an explicit measurement operation, not on
ordinary hover. A controlled point-to-triangle-grid benchmark checks both the
distance and witness point over twenty repeated queries: 2,500 triangles took
0.906585 ms/query and 40,000 triangles took 18.0042 ms/query on this Windows host.
This establishes a repeatable baseline, not an application-wide bottleneck.

No persistent cross-operation index was introduced. Such reuse needs a proven
source lifetime/invalidation contract and a representative slow interaction.
Skipping an unchanged measurement record would also skip checking whether its
referenced geometry changed. Existing geometry/reference validation is retained.

## Deliberately retained work

- General View mesh/presentation separation still requires complete frame,
  transparent/hidden-edge, picking and reference equivalence measurements. The
  proven unused index upload was removed separately.
- Assembly Drawing sources retain full Workspace invalidation. The narrower
  Part rule does not prove a complete dependency set for nested Assemblies.
- Assembly native scene validation remains because it checks cuts, targets and
  dependency geometry. Sketch packet conversion avoids redundant text processing
  without deleting validation.
- No additional product files were removed without proof that they are unused.
- Linux validation and packaging remain explicitly deferred to the Linux host.

## Regression fixture maintenance

Import fixtures now assert the agreed explicit-Regenerate behavior: importing
material metadata does not eagerly overwrite physical Relations parameters.
DXF fixtures distinguish ordinary points/curves from construction geometry and
expect ten manufacturing curve entities in the current rational/trimmed fixture.
Native library fixtures open Part-based templates through their Body-owned Sketch
API. CLI hatch checks select the exact Body identity rather than assuming the
first row is the imported Body. These changes retain geometry and persistence
assertions instead of weakening product behavior to match obsolete fixtures.

Localization review: these changes introduce no user-visible strings. Existing
five-language coverage and live Fusion-theme/font checks remain acceptance gates.

The broader gate passed 35 of 37 checks on its first concurrent run. Assembly
refresh passed separately after a shared working-directory lock collision. The
publication fixture now verifies unshared allocation retention before creating
a session copy, then verifies copied-history isolation; it passed after rebuild.
All four critical fingerprint, native Sketch, session and Drawing reuse checks
passed separately. These focused gates are not a full-repository suite claim.
