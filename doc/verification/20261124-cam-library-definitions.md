# CAM library definitions, 2026-11-24

## Scope

This checkpoint extends native Part handling with the agreed `.toolz` extension,
dedicated mm/in start templates, CAM library paths, and generic editable tool,
machine and vise examples. It does not change the native Part/Assembly/Drawing
schema or the protected placement solver. The C++ implementation is shared by
Windows and Linux. Verification here uses Windows/MSVC, Qt 6.11 and OCCT 8.0.1;
Linux execution remains unverified.

Existing standard Parameters are retained. Descriptive stock and standard fields
are independent of explicit CAM numeric inputs, as reaffirmed by the user. The
generic library assets contain no manufacturer attribution. Body appearance is
authored yellow cutting geometry and red shank geometry, with origin at their
interface. The vise uses two native Sketches with stable role IDs, a fixed contact
at Y=0, a moving contact at Y=opening, and top edges at Z=0.

## Lifecycle and persistence

- New tool invokes the ordinary Part template factory, adds the second ordinary
  Body and activates the cutting Body. It does not calculate material.
- Parameters use the established metadata transaction; unchanged confirmation
  does not create an Undo entry. Explicit Regenerate applies Relations.
- Save retains `.toolz`; Save As preserves the extension, creates a new document
  namespace and preserves local role/geometry identities and authored parameters.
- File-manager/startup/native open, desktop registration and release asset
  enumeration recognize `.toolz`. Drawing source readers recognize closed tool
  files through the existing Part paths; no new projection algorithm is added.
- The native file contains all required data. Configured library directories do
  not become required sidecars of the sample Parts.

## Native checks

`zima_create_cam_library` authors into a new output configuration directory using
native templates and public commands, refuses to overwrite existing assets,
regenerates, saves and reopens each definition. It verifies:

- tool Body roles, cutter diameter, cutting and shank lengths, origin placement;
- three successive cutter diameter edits (12, 8, 10 mm), cutting-length and
  overall-length edits, and restoration to the original values;
- unchanged Parameters revision, independent Family Body suppression and generic
  preservation, Save As identity and parameter preservation;
- closed-file Drawing annotations and BOM source metadata;
- both vise Sketches, world-coordinate top/contact planes, and successive
  openings of 80, 120 and 100 mm with the fixed Sketch unchanged;
- machine parameter persistence and an ordinary editable Body.

Native document tests cover mm/in templates, fresh document/Body IDs, active
cutting Body, unchanged template files, and standard native file contracts.
Portable settings tests cover CAM path defaults, configuration-relative overrides,
save/reload and correct `[CAM]` storage. Desktop integration tests verify all seven
extensions without replacing an existing default application. Packaging tests
include `.toolz` in factory native asset enumeration (16 tests, 2 platform skips).
Drawing title/BOM commands, Drawing dimension commands and nested-copy reference
regressions also pass with the updated native source dispatch.

## GUI checks

The CAM verification harness extends the existing setup regression with all five
languages in the global CAM settings page; creation through the real GUI factory
from both start-tool templates; active Body commands; root-level command rejection;
shank Sketch ownership; ordinary Save and native reopen; and the factory tool
viewer. The Windows candidate passed 204 checks, including both ordinary GUI
tool saves and all five language/path combinations. The captured factory tool
has the intended yellow cutting Body and red shank Body.

The inch start-tool template uses the canonical `mass = model.mass` default.
The source inch Part template's historical cancelling unit arithmetic rejected
unavailable mass when saving an empty new tool. Only the newly authored tool
template is corrected here; ordinary existing Part templates and calculation
precision are unchanged.

## Authoring references

The sample tool dimensions and independently rounded cutting values were checked
against primary technical data, without assigning the original brand to the
generic asset:

- [10 mm end-mill specifications](https://www.haastooling.com/p/03-0535).
- [Metric cutting table, page 4, conventional side milling, 10 mm / ISO N 23-25](https://www.haastooling.com/content/dam/haascnc/ecommerce-assets/linedrawings/milling/end_mills/speeds-and-feeds/03-0530%20to%2003-0539%20End%20Mills,%20HSAM2,%20Speeds%20and%20Feeds,%20Metric.pdf).
- [Machinable jaw dimensions: overall width 101.6 mm and height 44.5 mm](https://www.haastooling.com/p/05-0084).

The opening and machine limits are authored generic examples. No solid vise,
flute geometry, machine-specific postprocessor, setup import, clamping collision
support or complete CAM tool/operation UI is claimed by this checkpoint.
