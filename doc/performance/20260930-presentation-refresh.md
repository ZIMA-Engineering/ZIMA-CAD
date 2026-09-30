# Selection-filter and dimension-inspection refresh — 2026-09-30

This is a limited A4 follow-up to the
[application audit](../PERFORMANCE_AUDIT_20260930.md), measured after baseline
commit `c89575644511e7b6bd844596c5acb0b2bf3c18df`.

## Measured cause and change

Ordinary Tree selection already avoids rebuilding the scene. Changing the
toolbar selection filter, however, previously called `refresh_scene()` after
updating the viewer filter. This refreshed dependencies, rebuilt the Tree and
replaced the mesh even when only the offered selection kinds had changed.

For an ordinary Part/Assembly selection context, filter changes now update only
the offered kinds and preserve the existing exact Body/occurrence predicate and
hover-advancement policy. The kind mapping is shared with full scene creation;
its existing contents and ordering are unchanged. The fast path requires the
current contract to match the previous ordinary filter. Properties, Sketcher,
reference entry, measurement and other active command contexts retain their
existing full-refresh path. No command placement contract was changed.

If clearing a feature-dimension inspection has already rebuilt the scene inside
the filter's empty-selection callback, a second identical refresh is omitted.

Finishing Assembly placement-dimension inspection now hides the existing
annotations through their visibility filter and clears View/Tree confirmation
and the selected component Origin. It does not rebuild geometry or Tree items.
Finishing feature/construction parameter dimensions retains its full rebuild,
because those annotations are generated as part of the feature display packet.

No cross-action geometry cache or deferred update queue was introduced. Geometry
edits, source refreshes, document activation, visibility changes and Undo/Redo
continue through their normal update paths. Toggling the global Selection button
back on still uses a full refresh; this broader command-state restoration was
deliberately left outside the change.

## Measurement

The GUI fixture contains 256 occurrences of a calculated 10 mm profile solid.
The initial baseline recorded one mesh replacement and one Tree reset for every
filter change, taking 343–365 ms per change. Finishing Assembly dimension
inspection took 334 ms, also with one mesh replacement and one Tree reset.

The changed ordinary-filter path performs neither replacement nor reset and
takes approximately 0.03–0.10 ms in the measured runs. Finishing Assembly
dimension inspection likewise performs neither replacement nor reset and takes
approximately 0.01–0.03 ms. These are synchronous event-handler timings, excluding
queued painting, GPU time and perceived end-to-end display latency. The large
reduction comes from removing unnecessary work, not accelerating geometry
calculations. The extended correctness fixture also includes a real nonzero
Assembly plane-offset dimension.

Environment: Windows x64, Intel Core Ultra 9 285HX, MSVC Release, Qt 6.11,
OCCT 8, Windows Qt platform with Fusion style. This is a synthetic fixture, not
a promise for every user document. See the
[raw before/after log](20260930-presentation-refresh.txt).

## Verification and reproduction

```powershell
cmake --build build/cpp-windows-release --target zima-cad-cpp
ctest --test-dir build/cpp-windows-release -V -R '^zima_cpp_refresh_scope_ui_contract$'
```

The dedicated GUI contract uses its own build-directory workspace. It checks
mesh revision and Tree reset counts, persistent Tree-index validity, camera and
scroll preservation, actual dimension picking before/after closing inspection,
and selection clearing. It exercises all seven filters in both Assembly and
activated-Part contexts, and checks that the active-occurrence predicate still
rejects another occurrence of the same source Part. It then changes the open
source Part without saving, checks immediate display in repeated occurrences,
hides a component, exercises Undo/Redo and explicitly regenerates. The open
source must remain authoritative over its older file on disk.

Related contracts cover toolbar filtering during real Part and component
placement entry, Pattern dimensions, model dimension layouts, component
properties/activation, Workspace source handling, viewer selection and
translations. No new product UI text is introduced; benchmark diagnostics and
documentation are English.

The local GUI/CLI build remains available through `zima-cad.bat`. This change
does not replace the published Windows `2026093004` archive.
