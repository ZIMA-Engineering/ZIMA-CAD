# Shared 3D controller integration

Status: local Windows implementation and finite SpacePilot acceptance completed,
2026-11-28. Native Linux and real NaviGo acceptance remain pending. See the
[verification record](verification/20261128-3d-controllers.md). This feature is
not part of the immutable Windows release 2026112801.

The requested scope is camera navigation and programmable command buttons for
multiple controller families, including 3Dconnexion and NaviGo. Manufacturing
hardware is outside this work. Device motion must not edit model geometry,
regenerate documents, move Assembly occurrences or create Undo transactions.
Buttons may invoke existing commands through their ordinary ownership,
availability and confirmation rules.

## Existing application boundaries

`cpp/modules/viewer/include/zima/viewer/mesh_view.hpp` exposes camera state,
standard views, fit, projection, fly navigation and cursor-orbit settings.
`cpp/modules/viewer/src/mesh_view.cpp` contains the current middle-button
pan/orbit and wheel-zoom behavior. Its orbit path maintains a fixed world pivot;
its wheel path clamps scale, anchors zoom and refreshes persisted-reference
presentation. An input integration must preserve these details and picking
invalidation. Calling a camera-state setter alone is not yet evidence of an
equivalent navigation path.

Trace and extract only the necessary shared navigation operations when
implementing the feature; do not simulate mouse presses or duplicate camera
equations in device adapters. Preserve existing middle-button confirmation,
dragging, cursor orbit, fly navigation and Render camera editing contracts.

Use `cpp/app/application_settings.hpp` and an internal `PropertiesSubWindow`
opened beside System Setup for preferences. Persist user profiles in installation
configuration, outside immutable version directories. No native model-format
change or geometry sidecar is needed.

## Architecture and capabilities

Each adapter reports device identity, supported axes, buttons and optional
display capabilities to a shared C++ service. Keep operating-system code at
the transport boundary; Windows and Linux use the same camera and command
logic. A VID alone is insufficient to identify a device or select a protocol.

Normalize continuous motion, discrete motion impulses and button transitions
as different event types. Include monotonic timestamps, units and validity;
define the coordinate convention explicitly at the adapter boundary. Do not
assume every device exposing six numeric fields senses six independent axes.

Continuous motion may be coalesced before a bounded render update. Discrete
zoom impulses and button press/release edges must retain their meaning and
must not disappear under a latest-frame-only policy. Reject malformed or
non-finite input. Cap elapsed integration after a stalled application, stop on
stale input, and clear motion on disconnect, focus loss and device changes.
Retain observed held buttons across focus changes so a held key cannot become a
new press merely by switching views; disconnect clears the button state too.
Device polling and parsing must not block the GUI thread.

Only the active eligible CAD view receives navigation. File Manager, browser
tabs and inactive application instances do not inherit a stale target. Define
ownership when multiple devices or application instances are active, and
ensure one physical event cannot arrive through two enabled adapters.

## Device adapters

- 3Dconnexion driver/SDK integration: use the supported action interface for
  application commands and icons where available. Verify SDK terms, supported
  OS versions and redistribution requirements before choosing dependencies.
- Legacy USB HID: support explicitly identified and validated report layouts
  through a narrow adapter. Use normal OS access; do not replace the system
  driver or install a bridge as an incidental setup step. Check coexistence
  with the official driver and prevent duplicate event delivery.
- NaviGo USB CDC: implement its published protocol independently. The current
  discussion describes 115200 8N1, no flow control, DTR/RTS enabled and newline
  CSV `x,y,z,rx,ry,rz`. Its Z values include discrete zoom steps. Confirm units,
  ranges, rate, button messages, framing and impulse semantics before freezing
  the parser. Handle partial/multiple lines and disconnect/reconnect. Do not
  send calibration or mode commands during passive discovery. RP2040 vendor
  identity alone must not cause an arbitrary serial device to be opened.

The user connected a device during this design discussion. Windows reports
`SpacePilot`, USB VID/PID `046D:C625`, present with status OK and signed Microsoft
HID drivers (`input.inf`). No running 3Dconnexion service/process or installation
in its standard Program Files locations was found. That initial inspection
established enumeration only. Subsequent physical motion, button mapping and
zoom-direction acceptance are recorded in the verification report above; the
display remains unsupported.
The original SpacePilot has a different support history from SpaceNavigator;
the manufacturer's discontinued-device table lists 3DxWare 10.4.10 as its last
extended-support Windows driver. Do not advertise current-driver compatibility
from successful USB enumeration.

A subsequent read-only Windows Raw Input/HID descriptor inspection succeeded
without registering for input or writing device reports. The device exposes
Generic Desktop / Multi-axis Controller (usage page 1, usage 8), six signed
16-bit axis values with declared logical range -500 to 500: X/Y/Z in report 1
and Rx/Ry/Rz in report 2. Input reports are seven bytes. Report 3 declares Button
usages 1 through 21; this is a descriptor range, not proof of 21 physical or
independently usable keys. Maximum output/feature lengths are 2/8 bytes.
Actual axis direction, neutral jitter, report timing, button correspondence
and optional display behavior still require live testing. These descriptors
support investigating a standard HID adapter, not claiming completed support.

## User controls and command routing

Provide shared speed, per-axis direction and sensitivity, dead zone, rotation
lock, pan/zoom-only behavior and reset controls. Establish stable orbit and
zoom anchoring for orthographic and perspective views. A Sketch or Drawing
profile should retain its intended planar navigation; free rotation must be
an explicit supported choice, not an accidental device default.

Profiles distinguish model, Sketch and Drawing contexts and may be overridden
per device. Enumerate actual button capabilities instead of assuming two
buttons or a fixed maximum. Bind stable command identifiers, not translated
titles or synthetic keyboard macros. Reuse the application's existing actions,
icons and localization; validate their identities before making them a
persistent binding contract. Trigger only currently available commands.
Press/release and repeat behavior must be explicit. Device buttons must not
bypass dialog confirmation or steal text entry from a focused control.

Optional device displays and driver radial menus are capabilities of a later
adapter stage. Basic camera/buttons support does not imply display support.
New settings, tooltips, errors and end-user help require cs/en/de/fr/ru in the
same implementation change; corresponding guide images use that UI language.

## Acceptance before claiming support

1. Capture device descriptors and verify real axis/button reports without
   changing firmware, drivers or calibration. Use simulator traces for the
   NaviGo protocol, then verify real hardware before marking it supported.
2. Test deterministic camera changes at different input/render rates,
   simultaneous axes, dead zones, inversion, impulses, stale input, large time
   gaps and malformed packets. Measure end-to-end latency and CPU use; verify
   no geometry calculation, document revision or Undo change from navigation.
3. Exercise actual GUI navigation in Part, nested Assembly context, Sketch,
   Drawing and Render; cover orthographic/perspective, fit, standard views,
   cursor orbit, fly mode, command dialogs and existing mouse controls.
4. Verify button bindings, disabled commands, context/profile switching,
   localization, restart persistence, multiple tabs/instances, hot unplug and
   reconnect. Ensure the existing mouse and keyboard contracts still work.
5. Record a finite matrix by device, transport, OS and driver version. Windows
   evidence cannot establish Linux support; simulator evidence cannot establish
   real device compatibility. Release only the accepted adapters/capabilities.

## Primary references

- [NaviGo integration discussion and protocol](https://github.com/EslamEldeknawy/NaviGo-3D-Controller/issues/1)
- [3Dconnexion SDKs](https://3dconnexion.com/tw/service/)
- [3Dconnexion action-interface requirements](https://3dconnexion.com/uk/wp-content/uploads/sites/1/2020/08/3Dconnexion-Certification-Requirements.pdf)
- [3Dconnexion discontinued devices](https://3dconnexion.com/uk/support/faq/3dconnexion-discontinued-devices/)
- [Windows Raw Input device information](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getrawinputdeviceinfoa)
- [Windows HID capabilities](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/ns-hidpi-_hidp_caps)

## Implemented boundaries

The shared service lives in `cpp/app/controller_input.*`, with pure motion,
framing and settings contracts in `controller_motion.hpp` and
`controller_preferences.hpp`. `controller_hid.cpp` contains the native HID
transport branches; `controller_serial.cpp` owns a bounded, dedicated serial
worker. No new SDK dependency or model format is introduced.

Windows uses foreground Raw Input for Multi-axis Controller collections
(Generic Desktop usage 8) from vendors 046D/256F. Axes and button capabilities
come from the HID preparsed descriptor. The Linux branch uses nonblocking evdev
for those vendors with all six relative axes. Its scale is currently 500 units;
native validation against the connected device remains required. Device IDs
include the native connection path; moving USB ports or switching OS may require
selecting/configuring a separate profile. The driver SDK, device display and
driver radial menu are not implemented.

NaviGo opens only the selected port at 115200 8N1, enables DTR/RTS and sends no
firmware/calibration commands. Continuous axes use an adjustable input range
(initially 100); Z=50 supplies one 1.15 zoom step. Partial lines, invalid numbers
and oversized lines are handled before dispatch. All Z steps in a received
chunk are retained while continuous motion uses its newest frame. No assignable
NaviGo button events are inferred from the six-field protocol.

Continuous input expires after 150 ms, independently for each axis/report.
Integration is capped at 100 ms; longer stalls discard motion. A precise 16 ms
GUI timer consumes input, while serial I/O remains on its worker. The physical
HID convention becomes camera-space pan X, pan -Z, zoom Y, pitch Rx, yaw -Rz,
roll Ry. The user selected pull-toward-user to zoom in on 2026-11-28 after the
physical SpacePilot check. Per-axis reversal is user configurable. Motion changes transient camera
state through the same navigation method as the existing mouse controls.

Profiles are per device and Modeling/Sketch/Drawing context. Sketch defaults to
rotation off; Drawing always consumes pan/zoom only. Settings changes remain
private until OK. Cancel leaves saved preferences and the input service intact.
The settings window displays raw input independently from navigation, which is
paused while that window is open. Existing command availability is checked
before every bound button press; visible dialogs and text entry block commands.

`Controllers/Preferences` stores a schema-1 JSON value through the established
configuration save path. It contains no native model data or external geometry.
All controller UI strings are in the five application catalogs.

## Verification entry points

- `zima_cpp_controller_input_tests`: framing, impulses, timing, preferences,
  button edges, focus ownership and disconnect.
- `zima_cpp_controller_camera_tests`: independent camera equations, fixed
  pivots, mouse wheel parity, frame rates, finite inputs and fly movement.
- `ZIMA_VERIFY_CONSOLE_ONLY=1 ZIMA_VERIFY_CONTROLLERS_ONLY=1` with
  `zima-cad-cpp --verify-startup --working-directory <isolated-directory>`:
  internal GUI, real enumeration, synthetic application input, saved settings,
  profiles, command availability and existing mouse navigation. The directory
  must contain its own `config.ini`; production configuration is not a test target.

Synthetic input and enumeration establish different facts from physical motion.
Record real SpacePilot motion/buttons, native Linux behavior and real NaviGo
hardware separately before claiming those capabilities as verified.
