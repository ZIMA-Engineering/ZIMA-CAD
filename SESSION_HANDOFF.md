# Development handoff — 2026-09-15

## Current application

ZIMA-CAD is a native C++/Qt application. Python, its old runtime and its packaging
tools have been removed with the user's approval. The old migration and cutover
checklists have been retired; previous source and documentation remain in Git.
Use [native architecture](doc/CXX_ARCHITECTURE.md),
[native behavior](doc/NATIVE_BEHAVIOR_CONTRACT.md) and [AGENTS.md](AGENTS.md).
All project documentation must remain English; application localization is separate.

Recent feature work is documented in [Holes](doc/HOLES.md),
[work-plane selection](doc/WORK_PLANES.md) and the
[Assembly rotation arm](doc/ASSEMBLY_ROTATION_HANDLE.md). The shared View toolbar
starts with Regenerate, followed by the CAD console toggle using the terminal
icon from ZIMA-CAD-Parts. GUI, console and CLI use shared model operations;
[command coverage](doc/CAD_COMMAND_COVERAGE.md) records the current scope.

## Distribution and Linux continuation

Read [the binding distribution rules](doc/DISTRIBUTION_CLEANUP_PLAN.md) and
[the Linux release handoff](doc/LINUX_RELEASE_HANDOFF.md) before changing packaging.
The user will finish Linux support on Linux. Do not install WSL as part of this
Windows task. The removed Conda runtime was still referenced by Linux development
presets, so replace that dependency setup on Linux before building there.

The native release launcher, packager and updater are not yet implemented to the
new versioned layout. Do not publish old-style development builds as compliant
releases. Current Windows native dependencies remain available in the build/vcpkg
setup. A fresh GUI/CLI build and console UI contract passed with the old runtime
absent (see the Linux handoff for validation details).

## Work order

New explicit user instructions take precedence. The agreed Part sequence is in
[ROADMAP.md](ROADMAP.md#agreed-next-steps-for-part-2026-09-06), beginning with
Sketcher offsets, especially projected STEP geometry. Current offset support is
in [SKETCH_OFFSET.md](doc/SKETCH_OFFSET.md); review remaining practical coverage
before treating the historical plan as unimplemented work. The comprehensive
Undo/Redo audit stays after the agreed modeling features are broadly implemented.
AI integration is a separate next stage after the shared CAD commands.

Preserve current user configuration and unrelated working files. Required native
fixtures under `tests/fixtures/cross_language` are still consumed by C++ tests;
the historical directory name is not a reason to delete them.
