# Refreshing references in owned Sketches

Part regeneration previously visited standalone Sketches and embedded Sweep2D
Sketches, but skipped internal Sketches in Sweep3D, Helical Sweep, Hole, and Thread.
The same gap affected context-reference refresh, context-dependency discovery
before Assembly regeneration, and dependency summaries for open Parts.

## Shared traversal

`visit_document_sketches` now also accepts a Part/Assembly candidate directly.
It can inspect standalone Sketches, all supported owned profiles, and section
Sketches without copying Workspace. Read traversal may stop before loading unrelated
profiles. `update_document_sketches` uses the same scope for a private explicit-
calculation candidate and writes only changed data.

Extrusion/Revolution retain their existing document-list Sketches. These remain
the only two supported solid-cut types in Assembly; no Assembly Sweep or Hole
support is added. A section Sketch in a multibody Part can refresh references to
original objects in any of its bodies.

Context refresh continues to read already calculated sources and shares source
loading among Sketches in the same context. It invokes neither body calculation
nor mate solving and rewrites only changed profiles. Open-Part dependency summaries
now include internal Sketches and sections, preserving them when a reference
refresh is undone.

## Verification

- The original implementation failed the new test: internal Sweep3D context
  references were skipped (0/1 in 0.13 s).
- The new test visits all persisted Sweep2D, Sweep3D, Helical Sweep, Hole, and
  Thread profiles. For a rational quarter circle, it checks 257 points against
  the circle equation after a 0.01 mm source translation, trim identities, and offset parameters.
- Losing an edge preserves the last curve and marks the reference broken;
  restoring its original identity repairs it. Coverage also includes section
  Sketch refresh, standalone native Assembly-cut Sketches, an untouched adjacent
  profile, and Assembly-dependency retention when undoing command-driven refresh.
- An actual CLI process opens an Assembly, activates a Part containing a calculated
  helix, regenerates the Assembly, and saves the Part. Reloaded `.prtz` contains
  updated exact reference poles in the helix's base Sketch. The Part has no root
  Sketch, and body volume remains unchanged.
- The expanded targeted suite passed **4/4 in 21.13 s**. The complete GUI/CLI build
  then passed **116/116 tests in 501.40 s**, including the untouched adjacent-profile
  check. Logs: `build/owned-reference-all-build.log` and `build/owned-reference-full-tests.log`.

Context creation/detachment with a shared Part/Assembly-dependency commit remains
unfinished. This stage also does not yet safely remove aggregate dependencies
shared with closed source Parts.
