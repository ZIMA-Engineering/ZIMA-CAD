# Section Properties work-plane preview

Baseline: `f721bb73ff6f099b5f013492d737e70fff58920a`.

Section Properties published only a Point carrier for its editing Origin.
Consequently, selecting XY/XZ/YZ did not publish the highlighted Sketch work
plane. The cutting-surface visibility option is independent and cannot replace
this editing aid, especially before the Section Sketch contains a line.

The command now consumes the existing Plane preview carrier with the Section's
resolved Sketch origin, normal, local plane and container rotation. Its border
uses the ordinary feature-preview highlight. Section Properties retain the
container Origin and omit the Plane carrier's additional offset-point marker;
Sketch point editing remains inside the embedded Sketch editor. Closing the
dialog retires the transient plane. Shared placement, the solver, native formats
and calculated material geometry are unchanged.

## Verification

Windows Release, Qt 6.11.0, OCCT 8.0.1:

- Extended `ZIMA_VERIFY_SECTIONS_ONLY`: empty Sketch with hidden cutting surface;
  repeated XY/XZ/YZ changes before and after three-axis rotation; translated
  XZ plane; whole-Origin entry; reopening existing Sections; cutting-surface
  visibility independent of the work plane; unchanged OK without a revision;
  removal of the editing plane on close.
- Border points checked against the resolved Sketch plane and its center.
  Light -> Dark -> Light framebuffer comparisons prove visible highlighting in
  Part and Assembly. The visual comparison explicitly frames the work plane and
  restores the preceding camera and viewport anchor afterward.
- No Section calculation-count or document-revision increase from switching
  the work plane. Complete existing Section GUI flow passed, including embedded
  Sketch entry/return, local Undo/Redo, Cancel, middle-button confirmation,
  Drawing hatching/PDF, external axis references and repeated Assembly instances.
  Final candidate run: 20.99 seconds (whole suite, not command latency).
- Native Section contract, properties command, reference command and command
  suites: 4/4 passed. Coverage includes areas/holes, reversal, component
  exclusions, exact nested references, persistence and Undo.
- No user-visible text added or changed. Translation source coverage, catalog
  validation and runtime checks passed for cs/en/de/fr/ru.

Local evidence: `build/section-work-plane-20261123/`, including the successful
`gui-202721` run, plane framebuffer captures and `translations.log`.
User documents and configuration were not used as test outputs.

Linux compilation and GUI verification remain unperformed. This is a local
development correction, not evidence of a newly published portable release.
