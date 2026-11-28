# 3D controller verification — 2026-11-28

The local Windows implementation adds a shared C++ input service, internal
settings dialog, device/context profiles, command mappings and native HID/serial
transports. It changes transient navigation and user preferences, not native
model formats or the protected placement contract. This is development work
after immutable Windows release 2026112801; it is not a replacement release ZIP.

## Windows acceptance

Build: MSVC 18, Release, Qt 6.11.0 from the selected official kit; existing
OCCT 8.0.1 dependency retained. The root `zima-cad.bat` still selects the local
build. No controller SDK, replacement HID driver or firmware was installed.

| Check | Result and finite scope |
| --- | --- |
| Input contracts | Passed: JSON settings, bounds, planar-context defaults, chunk/split framing, invalid/nonfinite and oversized serial input, retained Z impulses, dead zones, inversion, rotation lock, 30/60/120 Hz integration, per-axis freshness, stalls, focus changes, button edges, disconnected/removed devices. |
| Camera equations | Passed: pixel pan, independent quaternion result, repeated fixed world pivot, anchor-preserving zoom, ordinary wheel parity, nonfinite input rejection, 30/60/120 Hz rotation and perspective fly movement. |
| Existing camera | Passed: Origin baseline regression; real GUI cursor orbit in Ortho/Perspective, successive gestures, middle/right pan, default orbit, short middle click, fit and fly. |
| Settings GUI | Passed in cs/en/de/fr/ru: native internal subwindow, live diagnostics, capabilities, profile changes, Cancel, OK, reopen/persistence, middle-button double-click confirmation over the document view. |
| Command ownership | Passed: active view dispatch, held-button edge only, disabled action rejection, text-entry blocking, and suspended navigation in settings. |
| Drawing | Passed in all five GUI runs: Drawing context, exact pan displacement, zoom ratio, ignored rotation and synthetic controller pan/zoom. |
| Model invariants | The automated model and Drawing navigation checks leave document state/Undo unchanged and add zero body calculations. |
| Localization | Catalog/source coverage and placeholder checks passed. Ten native command/dialog screenshots are captured separately in the five UI languages; pixels were not translated or redrawn. |
| Shared help | Generator/check passed: 806 HTML pages, 17,096 local references, 146 command guides, 11 overviews, all five languages, and exact local/runtime output inventory. The new controller guide is also accessible from its toolbar icon's context menu. |

The first standalone camera-test launch overlapped runtime deployment and failed
before its test code with an incoherent Qt runtime. No failed assertion was
suppressed. Controller/camera verification executables now use an isolated
runtime directory; deployment completed, official Qt binary hashes matched, and
all three focused native tests passed. Catalog validation also passed afterward.

## Physical SpacePilot

Windows enumerated the original SpacePilot, VID/PID 046D:C625, through Microsoft
HID. Its descriptor offers X/Y/Z and Rx/Ry/Rz, and button usages 1–21. This
descriptor range is not a claim that every physical key has been tested.

During a 180-second observation, real nonzero input arrived in all six axes,
the camera moved, document state remained unchanged and no body calculation was
added. The user confirmed responsive camera control. At the user's request the
default HID depth axis was reversed: pulling toward the user zooms in, pushing
away zooms out. Both platform HID branches use that same sign convention.

During a second 120-second observation, all six axes again received real input.
The user confirmed the revised zoom direction. The polling snapshot recorded
button 1; the user also configured button 2 to Save and confirmed that pressing
the physical button saved the fixture. The native file and saved binding were
observed. A held-state snapshot can miss a short press while a synchronous
command processes events, so this is not a complete raw button-report trace.

The second session's original whole-interval "unchanged document" assertion
failed because Save intentionally changed the dirty/saved state. This result is
retained as a limitation of that diagnostic, not reported as a passing no-change
test. The opt-in observer now checks document state and calculation count around
each navigation callback, and records requested controller commands separately.
The established automated no-change tests were retained. The user's button-2
Save binding is preserved in private local settings, outside Git.

## Remaining platform/device coverage

- Native Linux compilation, desktop permissions, evdev reports and serial
  reconnect require the user's Linux boot. Windows does not establish them.
- NaviGo framing/motion integration is tested with synthetic documented input.
  No real NaviGo device or author-supplied simulator was available.
- SpacePilot display output, driver radial menus and the 3Dconnexion SDK are not
  implemented. Official-driver coexistence, other device models, all 21 button
  usages, physical hot unplug/replug and multiple active instances are unverified.
- The camera equations cover fly and cursor orbit; full authored Render editing,
  nested Assembly activation and Sketch editing were not physically exercised
  with the controller in this session. Shared profile defaults and ownership
  checks are covered within the finite automated scope above.
- No end-to-end latency or CPU speedup is claimed. Timer-frequency independence
  is a deterministic mathematical check, not a hardware performance benchmark.

## Reproduction

Build `zima-cad-cpp`, `zima_cpp_controller_input_tests`,
`zima_cpp_controller_camera_tests` and `zima_cpp_origin_camera_tests`. Run their
CTest entries and `zima_cpp_translations_contract` using the selected SDK.
For GUI verification, create an isolated directory with `config.ini`, set its
language and an absolute `Paths/Localization`, then set
`ZIMA_VERIFY_CONSOLE_ONLY=1` and `ZIMA_VERIFY_CONTROLLERS_ONLY=1` and run
`zima-cad-cpp --verify-startup --working-directory <directory>`.

`ZIMA_VERIFY_CONTROLLER_LIVE_SECONDS=120` selects the optional physical observer
instead of synthetic GUI input. It creates an isolated fixture and writes its
diagnostic records in that directory. Native Linux follow-up is recorded in
[the handoff](../LINUX_RELEASE_HANDOFF.md).
