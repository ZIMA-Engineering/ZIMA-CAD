# Sketch axis overlap and native circle-point merge verification

Windows native C++ development build, OCCT 8.0.0, 2026-10-15.

## Changes

- Body activation preserves the expansion state of existing Tree rows.
- Active Sketch wires and previews are painted after Origin axes. Outside Sketch editing, overlay order remains unchanged.
- Native segments and curves precede built-in axes in ordinary and placement picking. Native points retain priority; axes remain available for candidate cycling.
- Drawing from a point already on an unbounded axis does not offer a redundant midpoint-on-axis M. Genuine midpoint inference on finite supports remains available.
- Merging a free segment endpoint into an existing PointOnCircle node seeds the surviving endpoint at the existing contact before solving. Selection order still determines the surviving native identity unless the established fixed/external anchoring rule applies. Existing circle C identity and source geometry are preserved. Conflicting joins retain the existing transactional rejection path.

No document-format, kernel, shared placement, geometry-side or user-visible text changes. Existing translation keys and symbols are reused. Five-language localization verification passed in the Windows GUI.

## Native regression matrix

All ten targeted CTest suites passed after the merge fix (17.88 seconds): viewer contracts, Sketch serialization, Sketcher contracts, keypoint contacts, reference commands, dimension commands, relation commands, mirror/tangency, curve commands and Sketch commands.

The new circle endpoint merge matrix contains 128 combinations: both selection orders, ordinary/construction segments, horizontal/vertical orientation, both radial sides, R/D, driving/reference and locked/unlocked dimensions. It independently checks axis and circle equations, fixed center, surviving segment/point identity, original C identity and serialized reopen. Driving dimensions are edited successively to radii 14, 16 and 15; three drag attempts follow each case. Locked reference dimensions are rejected without mutation before checking the supported unlocked reference state.

**Known failing editable variant:** the 64 reference-dimension cases reject all three radial drag attempts (192 attempts) without document mutation despite a free radial degree of freedom. This is recorded as a manipulation limitation, not a successful movement test. Driving-dimension cases retain their equations during drag attempts. Do not infer full editability from the successful merge.

The existing C+K matrix passes 860 cases but still reports 144 existing K-only dimension-edit limitations rejected atomically. Its native/external conic, support-side, repeated edit, drag and persistence coverage is complementary; it does not establish that every new merge variant supports every manipulation.

## GUI and user document

Actual GUI mouse events on disposable fixtures verify drawing and selecting normal/construction horizontal/vertical segments over Origin axes, endpoint priority, three successive drags, Undo/Redo, saved Sketch identity, and Light/Dark/Light palette changes. Genuine midpoint inference remains covered alongside endpoint C preview and confirmation. The final endpoint suite and its external-intersection regression passed. The C+K GUI suite also passed four conics, both selection orders, common hover/cycling, Delete K, identity-preserving Undo/Redo, three endpoint drags and save/reopen. Circle creation GUI checks passed upper/lower native endpoint snaps with shared C/T identity and native save/reopen.

For the clarified FORM-KOULE case, the test loads the first Sketch, adds a disposable horizontal segment from (-10,0) to (23,0), and joins its endpoint to the existing native circle point at (15,0). Endpoint-first, circle-point-first and preselected-endpoint interactions pass, including Properties OK, save/reopen, shared native incidence and the independent circle equation. This verifies point-to-point joining, not a different vertical construction endpoint-to-curve operation.

Original Projects/FORM-KOULE.prtz remains unchanged:
SHA-256 561EFAB60CCE754D46E595B4BD15866D17D9E370E5B7E1D6CCC73A97362E003F.

## Verification limits

Linux build and GUI verification are not performed on this Windows host. The new merge matrix covers native circle contacts; external support and trimming behavior are covered only by existing regression suites, not by every new 128-case combination. Full GUI dimension editing for every new combination remains unverified. Native curve-first C entry is outside the existing command contract and is not added by this change. The GUI coincidence fixtures additionally collapse Body rows before activation and assert that they remain collapsed. Exhaustive Assembly/library activation combinations are not asserted here.

The wider C GUI run exposed a separate preselection limitation: selecting a native endpoint before starting C can leave the entire native circle unavailable as a second input (candidate missing in support kind 2, order 2). The point-first command path passes. This does not describe the clarified point-to-point FORM join, which is tested separately in all three input modes. The failing full-run log is build/sketch-final-coincident-full.err.log; this variant is not counted as passed.
