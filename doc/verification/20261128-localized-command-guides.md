# Localized shared command guides

## Scope

The shared website/CAD documentation contains 11 introductory visual guides and
145 action-specific right-toolbar guides. All prose, image descriptions,
annotations, navigation and command titles have Czech, English, German, French
and Russian versions. The command inventory includes context-dependent Symbol,
title-block and thermal FEM actions, and documents the constraint choices under
their parent command. Development documentation remains English.

Each action uses its stable `QAction::objectName()`. The same metadata registry
drives the static website, Help, contextual Show guide and File Manager. The
three former generic Sketch/Extrusion/Revolution aliases remain mapped. The
return-to-Assembly action now has an explicit stable ID.

## Authentic images and review

The image inventory in `../guides/images/capture-manifest.json` records 955
unaltered native captures: 725 command buttons, 175 command-dialog images and
55 overview images. Every image has a language and SHA-256 identity. Command
button text must match the corresponding current localization catalog title.
The website checker verifies that public and packaged image bytes match this
inventory and that pages reference their own language's images.

Captures use fresh localized workspaces or freshly translated dialogs. Disposable
fixture names use neutral identifiers such as P01, B01 and F01. Authored data,
native identifiers, units and NC syntax are not translations of UI labels.
The opt-in capture hooks require an absolute `ZIMA_CAPTURE_GUIDE_DIALOGS`
directory; command-button collection additionally requires `--verify-startup`.
Normal application use creates no capture timer or files.

Dialog capture uses a 1920 by 1080 logical parent window. The full layout matrix
passed 660 cases across five languages and three parent sizes (92.96 seconds).
All 220 dialog metadata records passed layout checks. Review of 10,815 visible
text-bearing widget instances found no cross-catalog language leaks. Previously
clipped controls in pattern, helix, revolved-sheet and sheet-profile captures
were visually checked again at the larger size. Both sides of the revolution
fixture use Revolve; the shared Modeling type selector retains its actual
current Extrude label. Screenshots do not redraw or relabel that control.

The overview markers for Feature and 2D Sweep use the recorded native widget
and table-cell bounds. Other markers identify the visible regions of the actual
captured scenes. The FEM example shows a rod, source B1, analysis B2, restraint
C1 and force C2 of 100 N. The CAM slider interpolates within the selected motion.
The lightbox offers localized fit and actual-size views with keyboard-accessible
scrolling and a persistent Close control.

## Verification boundaries

The capture runs exercise existing native fixtures for new documents, Sketch
confirmation/cancellation, component placement, drawing properties, Render,
CAM, FEM, edge treatment and sheet FORM workflows. These are scoped UI and
documentation checks, not exhaustive modeling-algorithm certification. No
modeling algorithm, placement solver or native file format changes are included.

Two stale fixture assumptions were corrected without weakening their assertions:
the Sweep rejection example now uses a disconnected path, because a connected
offset path is supported; the Assembly origin-visibility example uses a Point
reference Feature, because new material Extrude features belong to Parts.

Windows packaging now exports the committed source before copying runtime
configuration, resources and public website files. This prevents test-generated
staging files from entering the package. The previous staging tree was audited
against its recorded Git commit; 165 disposable test files were preserved outside
that source tree before reuse. Official candidate checks still verify source
identity, runtime dependencies, native factory assets and the signed package.

Linux execution is not covered by this Windows verification. Website pages use
plain HTML, CSS and JavaScript and are identical in both packaging paths.

## Completed website and CAD checks

The public generator and checker passed 801 HTML pages and 16,986 local
references. The 1,927-file public inventory is identical to the offline runtime
copy; all 955 image hashes and language assignments passed.

The Windows localization, documentation, browser workspace and File Manager UI
contracts passed (4/4; 51.51 seconds). The documentation contract opened the
command guide from the native context menu in each of the five languages and
checked the page language, command identity, procedure and both loaded native
images.

Browser review checked English search and workspace filtering, the same command
in all five languages, Czech interactive diagram markers, fit/actual-size image
viewing, French dialog text and a 390-pixel Russian mobile layout without page
overflow. The actual-size mobile image remained scrollable with a visible Close
control. No browser console errors were reported during these checks.

The output manifest is replaced atomically and unchanged builds no longer
rewrite it, preventing a local preview rebuild from exposing a partial manifest
to concurrent validation. The obsolete eleven shared-language source/public
screenshots were removed after their localized replacements were verified.
