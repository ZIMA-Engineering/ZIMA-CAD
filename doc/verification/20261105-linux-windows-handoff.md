# Linux findings for Windows follow-up (2026-11-05)

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
