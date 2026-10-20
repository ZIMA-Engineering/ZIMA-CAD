# Factory sources after an update - 2026-10-20

## Defect and scope

An installed File Manager resolved relative built-in library sources against
the writable installation-root configuration. Factory libraries are actually
inside the selected immutable version. Engineering Tables could therefore be
reported unavailable even when the release contained them. In addition, a
previously saved complete source list prevented new factory sources appearing.

The correction is limited to GUI application settings source resolution and
completion. It preserves explicit custom paths, existing source order, disabled
states, read-only preferences, project overrides and ordinary resource-directory
settings. Loading does not write configuration. Materialized default paths are
recognized only inside the same installation's managed platform/version layout.
Native document formats, library assets and container placement are unchanged.
No updater extraction change was necessary.

## Windows verification

- The new portable settings fixture failed before the correction with
  `Fresh factory source resolved outside the active version`.
- `zima_cpp_portable_settings` passed after the correction: all seven built-in
  roots, a previous version without Engineering Tables, an older saved source
  list, relative/absolute custom sources, explicit factory-path overrides,
  materialized old defaults, disabled/read-only states, save and a further
  version switch. Existing GUI/CLI settings layering checks also passed.
- `zima_file_manager_portable_sources_ui` passed. An actual press/release on the
  new source opened the current version's Engineering Tables and displayed its
  Fasteners directory metadata. The old shared `config/lib` was absent.
- The existing complete `zima_file_manager_ui` passed in 81.95 seconds. Its CTest
  working directory now points to the repository required by its existing
  relative branding fixture. The new fixture resolves the configured source
  label instead of assuming an untranslated identifier.
- `zima_cpp_translations_contract` passed. Localization review found no new or
  changed user-facing strings; existing labels remain shared and translated.
- The isolated signed updater test
  `Updates.test_update_installs_previously_absent_factory_directories` passed:
  installation created a new nested factory library directory, preserved custom
  configuration/library bytes, and retained them through rollback.
- The locally retained signed 2026102002 archive includes 13,696 Engineering
  Tables members across source and runtime. The reported defect does not by
  itself establish that a particular remote update failed to download files.

Evidence: `build/factory-sources-before.log`,
`build/factory-sources-regressions.log`, `build/factory-sources-ui-final.log`,
and `build/update-missing-directories.log`. Initial GUI test failures were test
setup errors corrected as described above, not accepted results.

This is finite Windows verification using shared C++ code. Native Linux
execution and acceptance of the next committed release package remain pending.
