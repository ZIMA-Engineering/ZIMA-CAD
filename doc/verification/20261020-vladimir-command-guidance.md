# Vladimír command guidance - 2026-10-20

## Implemented scope

The application now presents its existing command status messages as transparent
upper-left canvas text, introduced by the green proper name **Vladimír**. The
main text uses the existing View theme foreground. There is no background panel
and the widget cannot take keyboard focus or intercept mouse interaction.
The initial ready message offers object selection or starting a command.

The existing workspace status label forwards changed messages synchronously;
there is no polling, model traversal, scene publication or geometry calculation
for guidance. The same overlay follows the active model or Drawing canvas.
Existing command semantics and the status bar are retained. This first version
consumes existing status guidance and operation errors; it does not claim a
complete newly authored help sequence for every command or an AI diagnosis.

Relations additionally publishes missing-dimension line/identifier diagnostics
and an actionable instruction to check the reference. Its existing debounced
parser reports syntax errors and returns to the normal instruction when those
issues are cleared. Closing the editor restores the ready instruction.

`[Application] Vladimir=YES` is the default; `NO` disables guidance. Global
Settings exposes a localized checkbox, applied immediately after OK and saved
through ordinary portable settings. Cancel does not save the pending toggle.
An absent setting defaults to enabled. No native document or shared placement
contract changes are involved. User project/library/configuration files were
not replaced to introduce this setting.

## Windows verification

- `zima_cpp_command_guidance_contract`: real MeshView integration, deduplicated
  status forwarding, upper-left bounds, literal text, small canvas resizing,
  transfer to another canvas, disabled-message updates and re-enabling.
  Transparent rendering and the name accent are checked in both themes. Canvas
  hit testing remains unobstructed; the OpenGL framebuffer and camera are
  unchanged while the interactive QWidget capture includes guidance.
- `zima_cpp_portable_settings`: missing/default, mixed-case YES, NO and saved
  NO/reload. Existing layered settings and factory-source contracts pass.
- `zima_cpp_relations_editor_contract` and
  `zima_cpp_translations_contract` pass. All five catalogs include the new
  status/settings/help text and preserve placeholders.
- `zima_cpp_command_guidance_ui_contract`: isolated configuration and an actual
  calculated native Part, opening Relations with a missing d42, live diagnostic
  forwarding, Cancel and unchanged document revision. Fresh Global Settings
  controls are checked in cs/en/de/fr/ru; ten canvas captures cover those
  languages in both themes. Actual main-window Settings OK disables/re-enables
  guidance immediately and survives reload; Settings Cancel leaves it enabled.

Evidence: `build/vladimir-widgets-build.log`,
`build/vladimir-widget-tests.log` (four tests, 15.58 seconds),
`build/vladimir-workspace-build.log` and
`build/vladimir-workspace-tests.log` (integrated GUI, 4.78 seconds).
The Czech light/dark full-window captures in
`build/cpp-windows-release/command-guidance-gui/` were visually inspected.

The widget test uses a second ordinary canvas to verify transfer; a complete
real Drawing-command sequence is not added by this change. Other commands
retain their existing message coverage. Native Linux execution and acceptance
in a committed portable release remain pending.
