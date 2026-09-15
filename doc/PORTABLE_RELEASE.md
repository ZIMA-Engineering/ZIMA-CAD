# Portable release distribution

The binding requirements are in
[Native runtime and versioned distribution](DISTRIBUTION_CLEANUP_PLAN.md).
Build instructions, the approved directory tree, configuration ownership and
implementation status are in [Native distribution](NATIVE_DISTRIBUTION.md).

Use `ZIMA-CAD-YYYYMMDDNN.zip`; the root `VERSION` is the single identity source
for GUI, CLI, tag and manifest. Each version owns its native libraries, plugins,
factory configuration and resources. Exact committed source accompanies it.

Shared user settings live in root `config/`, with `windows/` and `linux/`
overrides. Projects, autosaves, recovery and custom builds are preserved across
updates. The root `config/` replaces the earlier `profile/` proposal. Configuration
backups and future conversion/rollback rules are covered by the implementation
guide. Python/Conda is not part of the application runtime.

The Windows candidate builder, version launcher, signing tool and authenticated
updater are implemented. Settings exposes Updates, startup notification is a
small status-bar link, and successful activation retains current plus previous
per OS. See [Application updates](UPDATES.md). Platforms may publish independently;
Linux execution must be completed on Linux. A local successful build is not proof
of all portable release gates; published archives are immutable.

Windows: [WINDOWS_RUNTIME_AND_BUILD.md](WINDOWS_RUNTIME_AND_BUILD.md).
Linux: [LINUX_RELEASE_HANDOFF.md](LINUX_RELEASE_HANDOFF.md).
