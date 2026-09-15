# Portable release distribution

The binding distribution requirements were updated on 2026-09-15 to follow the
ZIMA-CAD-Parts versioning strategy. Their complete definition is in
[Native runtime and versioned distribution](DISTRIBUTION_CLEANUP_PLAN.md).

Use `ZIMA-CAD-YYYYMMDDNN.zip` with Windows/Linux version directories and exact
source. Each installed version owns its native libraries, plugins and resources.
Keep user profiles, Projects, autosaves and recovery outside version directories.
Keep the previous verified official version available and preserve custom builds.
Python/Conda is not part of the product runtime. Published archives are immutable.

This supersedes the earlier `ZIMA-CAD-YYYY-MM-DD.zip`, date/revision suffix and
unversioned shared-resource layout. The current embedded `RELEASE_DATE` mechanism
must be replaced coherently before the first package under the new convention;
that implementation is pending. No historical target date promises a release.

Before publishing, satisfy all dependency, source provenance, archive, extraction,
GUI/CLI, calculation and persistence gates in the binding requirements.
Windows details: [WINDOWS_RUNTIME_AND_BUILD.md](WINDOWS_RUNTIME_AND_BUILD.md).
Linux continuation: [LINUX_RELEASE_HANDOFF.md](LINUX_RELEASE_HANDOFF.md).

The C++ portable package builder, version launcher and signed updater are not yet
implemented. A local successful build or CMake install is not a validated release.
