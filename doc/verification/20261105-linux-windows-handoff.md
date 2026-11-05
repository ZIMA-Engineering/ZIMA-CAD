# Linux findings for Windows follow-up (2026-11-05)

## Follow-up: complete Drawing GUI passes on Xcb

The newly reached azure-text assertion was a pixel-measurement issue: the saved image showed correctly colored text, but the Linux font backend produced only two glyph pixels exceeding the test's half-coverage cutoff. The test now checks the expected azure color composited over the fixture's black background, including antialiased glyph coverage. It retains the glyph-region exclusion of support/handle strokes and does not change product colors, font sizes or rendering. Failed captures are retained for diagnosis.

The complete Drawing GUI suite now passes on the actual Xcb desktop (6.76 seconds): cardinal placement, rectangular selection, projection, dimension text/handle dragging, Cancel, middle-button interaction, native persistence and global library paths. The preceding failing run in the JSON record is historical and superseded by this full pass. Repeat/view-control/detail shutdown checks also passed on native Wayland as recorded above. Windows execution of the fixes remains open.

Separate experimental test changes did not resolve the browser bookmark and feature hover checks and were reverted; those findings are still open. Do not infer complete acceptance from the Drawing fix.

## Follow-up: Drawing lifetime fix verified on Linux

The shutdown blocker described below is fixed by releasing the GUI-thread depth renderer through Qt's application cleanup routine before platform teardown. GPU packets and shaders are released with the owning context current, and a previously current context is restored. The drawing algorithm and cached images are unchanged. All renderer callers were traced to GUI painting or GUI verification; vector export paths remain separate. No platform-specific drawing implementation, geometry tolerance, persistence contract or user-visible text was changed.

The old thread-local object outlived QApplication. Qt 6.8.2 invokes its post routines at the beginning of the QApplication destructor, before widget/platform cleanup ([Qt source](https://github.com/qt/qtbase/blob/v6.8.2/src/widgets/kernel/qapplication.cpp#L615)). Windows execution remains outstanding.

Both Xcb and native Wayland now pass repeat-region/dimension creation, view controls and detail UI suites, including normal process shutdown. Repeat dimensions cover 15 actual mouse creations across two views and five kinds, confirmation, Undo and native reopening. Controls and detail checks retain preview, Cancel, OK, editing, persistence and Undo/Redo assertions.

The material Save As and Drawing template-picker automation now waits for visible modal readiness instead of a zero-delay callback that could accept/reject before dialog execution. Material automation explicitly selects the fixture directory. The five-language desktop translation/material suite passes. These changes are limited to tests.

The full Drawing GUI suite now progresses past the template pickers and fails with `Selected dimension text is not azure`; this newly reached assertion remains unresolved. It is not a process crash. The broader outstanding findings below remain open. See the adjacent JSON record for the finite rerun results. No Linux release has been published.

## Source and publication state

This branch integrates Windows 2026110403 (`48b3b997a`) with Linux candidate work. VERSION 2026110404 is a candidate identity, not a published release. Do not publish or tag this candidate until remaining verification is complete. Linux uses Qt 6.8.2 and OCCT 8.0.1; Windows verification of these changes remains outstanding.

## Verified shared fixes

- Curve/plane contact root bounds now account for the precision of the double-precision input coefficients while retaining extended-precision arithmetic. Forty contact cases passed, including separated near-tangencies and two crossings; the contact-only comparison also passed with OCCT 8.0.0.
- Sketch dragging starts coupled retries from valid geometry and the requested projected handle, preserves established midpoint projection before curved-path fallback, and rejects unreachable axis-bound locked-radius arc endpoints. The handle mobility check distinguishes unrelated free geometry.
- The external trimmed-arc regression distinguishes locked driving R/D from editable unlocked driving and reference R/D. It checks actual movement, both endpoint equations, dimension role/value and immutable external sources.
- Helical result-mode tests use document-specific temporary files to avoid concurrent test collisions.
- Linux portable-font test configuration tolerates Qt installations without the static offscreen plugin target. Directory-picker translation automation waits for modal readiness.

Nine focused native suites passed after the final Sketcher change (19.47 seconds): native arc/axis (3,280 scenarios), tangent contact, midpoint profiles, external trim boundaries, mechanisms, engineering profiles, trim fragments, angle sectors and the general Sketcher contract. Their existing dimension/edit/drag/persistence checks remain enabled. This is finite coverage, not a claim of exhaustive GUI coverage. The two helical suites passed concurrently in the preceding run.

Current desktop checks passed curve/plane contacts (40 cases), arc/axis GUI scenarios 35–47, nested Assembly presentation, empty roughness, Loft properties in five languages, dimension layout and drawing breaks. A preceding full arc/axis GUI run failed scenario 35; a complete final 48-scenario rerun remains outstanding.

## Blocking Drawing shutdown crash: investigate on Windows

Reproduce on Linux from the repository root:

```sh
env QT_QPA_PLATFORM=xcb ZIMA_VERIFY_DRAWING_REPEAT_ONLY=1 gdb -q -batch -ex run -ex 'bt 20' --args ./build/cpp-native-release/zima-cad-drawing-harness --verify-measurements
```

The repeat-region, drawing view-controls and drawing-detail GUI checks terminated with SIGSEGV. The repeat-region backtrace identifies shutdown, not a modeling-kernel calculation:

```text
_XSend / XQueryExtension / XInitExtension (libX11)
libGLX_mesa / libqxcb-glx-integration
QOpenGLContext::destroy()
QOpenGLContext::~QOpenGLContext()
zima::app::DrawingDepthView::~DrawingDepthView()
__call_tls_dtors
exit
```

`cpp/app/drawing_depth_view.hpp` returns a function-local `static thread_local DrawingDepthView`. Its context is destroyed during thread-local teardown after the QApplication/platform connection has been destroyed. This is the leading lifetime diagnosis; a fix has NOT been applied. Trace all callers and arrange destruction of GPU resources with a valid context while the Qt platform is alive. Preserve cached rendering, depth visibility, vector output and the shared Windows/Linux implementation. Do not suppress the crash or leak the renderer as a substitute for lifecycle ownership. Confirm the diagnosis and verify both platform shutdown paths.

## Other open verification findings

The broad 328-test run before the final focused fixes had 246 passes, 81 failures and one not-run test. Do not describe the full suite as green. Six representative failures reproduced with the previous OCCT runtime; this was a runtime comparison, not a fresh complete baseline build. Some older fixtures expect immediate volumes or obsolete commands.

Desktop checks still need investigation: browser bookmark Edit does not open its properties in the test, hidden-window feature-axis rendering reports zero wire pixels, FORM insertion cannot find an editable internal driving dimension, and material Save As automation can hang. The latter has an uncommitted Linux test-helper experiment that is deliberately excluded from this commit because its final desktop rerun has not passed. Personal settings, projects and screenshots are excluded.

No new product-visible text is introduced by these changes. Existing shared translation keys are retained. Five-language Qt translation verification passed after the directory-picker fix, but the later full desktop run stopped at the separate material Save As automation issue; complete final localization/UI verification remains open.

Linux signed packaging, GitHub publication, public updater discovery and a final clean packaged-runtime check are not complete. Existing candidate archives are superseded and must not be published as final.
