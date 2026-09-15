# Standalone ZIMA-CAD command line

`zima-cad-cli` runs the same commands as the CAD console. It uses
`command_host::Host`, Workspace, and existing native operations without creating
QApplication or a main window. Shared PDF export uses QGuiApplication in `offscreen`
mode, Qt Gui/Svg, and the corresponding platform plugin. Each invocation has its
own Workspace; it does not connect to an already running CAD window.

Current coverage of 299 commands and verification results are documented in
[CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md); the GUI adapter boundary is
summarized in [CLI_GUI_AUDIT.md](CLI_GUI_AUDIT.md).

The new [Holes](HOLES.md) feature shares `holes.create/get/set` with GUI for finite
cylindrical channels defined by owned Sketch segments. [Work planes](WORK_PLANES.md)
describes shared automatic/manual plane selection for Sketches, profiles, and
construction Planes, including CLI.

## Build and first run

The Windows build script builds both GUI and CLI:

```powershell
./tools/build-windows.ps1 -Configuration Release
& ./build/cpp-windows-release/zima-cad-cli.exe --help
& ./build/cpp-windows-release/zima-cad-cli.exe --working-directory ./Projects --command "documents"
```

Create an empty Part from the start template and save it:

```powershell
& ./build/cpp-windows-release/zima-cad-cli.exe --working-directory ./Projects --command "new part cli_example" --command "save"
$LASTEXITCODE
```

The working directory must exist; the default is the process working directory.
GUI `Paths/WorkingDirectory` does not override this explicit CLI context. Subsequent
`open`/`save` commands set the working directory to the document directory, as in
the shared console host. `new` creates an in-memory document; only `save` creates
the file. `new` rejects an existing path.

Linux target from the same CMake project:

```bash
cmake --build build/cpp-release --target zima-cad-cli
./build/cpp-release/zima-cad-cli --working-directory Projects --command documents
```

This stage was built and run on Windows. The Linux branch uses POSIX I/O but was
not run at this stage. Configuring the whole CMake project requires Qt. Run CLI
from the build directory with Qt, OCCT, and C++ runtime libraries available;
command-driven graphical output needs the `offscreen` plugin. This is not a newly
verified portable release package.

## Input modes

Choose exactly one mode:

- `--command "command"`: repeatable; commands run in order in one Workspace.
- `--script file.txt`: UTF-8 text file with one command per line.
- `--stdin`: the same lines from standard input until EOF.

Commands may be plain text or JSON as described in the [CAD console](CAD_CONSOLE.md).
Empty lines and lines whose first non-whitespace character is `#` are ignored.
Scripts accept an initial UTF-8 BOM, LF or CRLF, and a final line without a newline.
Input lines are limited to 64 KiB; an oversized line is rejected as a whole, never
executed as several commands. UTF-8 is validated before mutation.

Example `batch.txt`:

```text
# Document paths are relative to the CAD working directory.
open "part with spaces.prtz"
context
tree
regenerate
save
```

Run on Windows:

```powershell
& ./build/cpp-windows-release/zima-cad-cli.exe --working-directory ./Projects --script ./batch.txt
```

Paths passed to `--script`, `--config`, and `--working-directory` are resolved
against the process directory at startup, independently of subsequent batch commands.
A direct UTF-8 script is practical for complex text/JSON and non-ASCII names in
PowerShell: its contents bypass shell quote parsing and pipe encoding. A stdin
client must send UTF-8 and read output concurrently.

## Output and errors

Each executed command produces exactly one LF-terminated JSON object on stdout:

```json
{"protocol":"zima-cad.commands/1","ok":true,"code":"ok","message":"","data":[]}
```

Output is sent immediately after each command without waiting for EOF or process
exit. There are no interactive prompts. C/C++ and OCCT diagnostics go to stderr;
stdout has a separate protocol descriptor. `--help` is an exception: it returns
readable help without loading config, documents, or the kernel. `--build-info`
returns the embedded JSON build identity and `--version` returns the build ID;
both exit before configuration or graphics startup.

| Exit code | Meaning |
| --- | --- |
| `0` | All executed commands succeeded, or successful help/empty batch. |
| `1` | At least one command failed; its JSON result contains details. |
| `2` | Invalid invocation, config error, or I/O error; diagnostics on stderr. |

By default, execution stops at the first error. `--keep-going` continues with later
commands but retains exit code `1`. Output failure always stops execution, so a
later `save`, for example, cannot run after the receiver disconnects.

A failed batch does not automatically roll back completed commands. Files explicitly
saved earlier remain saved; process exit saves nothing else. Invalid diagnostic
bytes from Windows/external libraries are replaced with Unicode U+FFFD in error
responses. A successful model response with invalid UTF-8 is rejected as
`invalid_result`; identities and names are never silently rewritten.

## Config and templates

`--config file.ini` selects the base config. Without it, lookup follows GUI order:
`config/config.ini` under the process directory, under the executable directory,
and `../../config/config.ini` relative to the build executable. Project `config.ini`
in the initial working directory overrides nonempty base-layer values. Relative
paths belong to the directory of the config supplying them. Settings load once
at startup and commands do not rewrite them.

CLI reads only required string values:

- `Application/Language`;
- `Paths/Templates`, `Paths/Localization`;
- `Templates/Part`, `Templates/Assembly`;
- `Units/Length`, `Angle`, `Mass`, `Time`, `Temperature`, `Stress`.

Other GUI settings are ignored. Supported UTF-8 INI values include ordinary strings,
quotes, and escaped backslashes. Use portable `/` paths or strings saved by the
config editor. Qt typed values are not CLI string settings. Config and catalogs
are read-only. Command translations come from the same `QtTranslations` section
as GUI; technical CLI errors and option help are English.

Existing documents can be read without a base config, but `new part`/`new assembly`
require the corresponding start templates. This stage changes neither native
`.prtz`, `.asmz`, `.drwz` formats nor start templates. It introduces no required
external geometry, revision, or cache files.

## Scope and verification

The catalog is shared with the console: documents, context/tree, new/open/save,
regenerate, undo/redo, fit, and create/get/set for all six basic primitives.
Without a View, `fit` returns an error. `context` invents no selection, hover, or
camera. Box, cylinder, sphere, cone, pyramid, and wedge dimensions are explicitly
in mm and use the same transactions as GUI. Connection to a running GUI, an AI
provider, and voice are outside this stage.

`zima_cpp_cli_process_tests` launches actual CLI processes. It checks QSettings-saved
config, project layering, templates/units, Czech paths, all three native types,
text/JSON, scripts and stdin, immediate output before EOF, stop-on-error,
keep-going, invalid UTF-8, long lines, and exit codes. It opens a box, explicitly
regenerates and saves it, and compares its 6000 mm³ volume and exact original-face
identities. Output loss is also tested through the result-writing adapter, ensuring
that a subsequent Save does not run.

Source files are `cpp/cli/main.cpp` (platform I/O), `runner.cpp` (arguments, batches,
shared host), and `settings.cpp` (config reading). Modeling logic stays in existing modules.

Verification: Windows Release, **57/57 tests passed** (422.09 s),
`build/cli-full-tests.log`. After final translation-context additions and config
backslash checks, CLI rebuilt and its entire process test passed separately
(`build/cli-final-focused-tests.log`). At that original stage, `dumpbin /dependents`
confirmed no Qt DLLs (`build/cli-dependencies.log`). Shared PDF export subsequently
introduced Qt Gui/Svg in headless mode; that original observation no longer describes
the current runtime.

### Modeling a box

```powershell
./build/cpp-windows-release/zima-cad-cli.exe --working-directory C:/CAD/example `
  --command "new part box_example" --command "box.create 10 20 30" --command "save"
```

This creates a real 10 × 20 × 30 mm box (6000 mm³) in `box_example.prtz`.
`box.create` returns a stable container ID. In a later invocation, open the Part
with `open` and use `box.get ID` or `box.set ID 15`. The same process can undo,
redo, and save changes. CLI does not transfer Undo history between processes.

Exact syntax, units, locks, and transactions are in the
[box command description](CAD_CONSOLE.md#shared-box-operation-2026-09-11).

`cylinder`, `sphere`, `cone`, `pyramid`, and `wedge` follow the same pattern.
`cylinder.create 3 6` creates a cylinder of radius 3 mm and height 6 mm;
`cone.create 4 0 6` creates a pointed cone. Full parameter order is in the
[basic primitive catalog](CAD_CONSOLE.md#all-basic-primitives).

### Drawing PDF without a window

```text
open drawing.drwz
export.pdf drawing.pdf
```

This exports all sheets without regeneration. An existing PDF requires explicit
JSON `overwrite: true`. GUI uses the same renderer; sheet dimensions, dimensions,
hatching, and fonts are not a separate approximate CLI implementation. Qt uses
`offscreen` even if `QT_QPA_PLATFORM` specifies another platform; no QWidget or
window is created. `--help` does not initialize graphics. CLI still does not control
another running CAD instance or automatically save native documents at exit.
See [DRAWING_COMMANDS.md](DRAWING_COMMANDS.md).

Drawing DXF uses the same command as Sketch DXF, with `sheet` instead of `sketch`:

```json
{"command":"export.dxf","arguments":{"path":"drawing.dxf","sheet":"SHEET_ID"}}
```

Find sheets with `drawing.sheet.list`. Output uses paper dimensions in mm and the
shared GUI/CLI renderer without geometry recalculation.

`export.image` creates a sheet image or exact crop from a PNG/JPEG path, sheet ID,
and optional DPI (default 150):

```json
{"command":"export.image","arguments":{"path":"detail.png","sheet":"SHEET_ID","dpi":254,"crop_mm":[30,10,60,40]}}
```

This example creates 600×400 pixels: the printed paper appearance on white,
requiring neither window nor camera. CMake also copies the JPEG plugin and its
dependencies beside CLI.

### Modeling command overview

`construction.list/get` reads construction geometry and 3D-curve point ownership
without calculation. Units, coordinate systems, and pagination are described in
[CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md).

`placement.get/set` reads and atomically edits numerical placement of bodies,
features, and constructions. Absolute values, corrections, and reference offsets
are distinguished in [PLACEMENT_COMMANDS.md](PLACEMENT_COMMANDS.md).

`construction.create/set` creates points, axes, and planes and atomically edits
properties and placement through the shared GUI Properties transaction, respecting
references, locks, and the active body. Arguments and remaining scope are in
[CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md).

Create 3D curves with `construction.create`, `kind: "curve3d"`, and `points`.
`construction.set` edits polyline/spline type, fillets, tangents, point properties,
and the complete point list while preserving supplied IDs. Omitted points are
removed. Geometry and Undo transactions are shared with GUI; exact arguments and
examples are in [CONSTRUCTION_COMMANDS.md](CONSTRUCTION_COMMANDS.md).

`sweep2d/sweep3d/helical.get/set` manages sweeps. Native standalone inputs support
`sweep2d.create` (path Sketch and profiles), `sweep3d.create` (3D curve and profiles),
and `helical.create` (base circle, radial path, and cross-section). Editing includes
stations, the complete profile list, 3D path, and 2D path-plane reference. It shares
Properties commit, explicit calculation, and Undo. Examples and ownership rules:
[SWEEP_COMMANDS.md](SWEEP_COMMANDS.md).

`thread.catalog metric M10` reads the thread catalog without an open document, even
outside the project directory. All dimensions are mm. Pagination and designations:
[THREAD_CATALOG.md](THREAD_CATALOG.md).

Native `FeatureKind::Hole` has `hole.create/get/set`: owned bore, chamfer, and tip
Sketches, thread wire, dimensions, and locks. Fixed-length, through, and `up_to` drilling with original target references are supported.
Details and limits: [NATIVE_HOLE_COMMANDS.md](NATIVE_HOLE_COMMANDS.md).

The current Opening has `opening.create/get/set` for plain or catalog-threaded
openings, pilot bore, chamfer, and tip. Units, examples, and coverage boundaries:
[OPENING_COMMANDS.md](OPENING_COMMANDS.md).

External shaft threads use `shaft_thread.create/get/set`: original cylinder and
start face, catalog, length, chamfer, and Up To/Through All end conditions.
Examples and contract: [SHAFT_THREAD_COMMANDS.md](SHAFT_THREAD_COMMANDS.md).

Standalone drill points use `drill_point.create/get/set`, a shared angle, and an
editable list of original bottoms. Example and generated-face identities:
[DRILL_POINT_COMMANDS.md](DRILL_POINT_COMMANDS.md).

`shell.create/get/set` manages shells. `shell.faces` returns available faces of the
real input, using a container ID when editing. An empty opening list means a closed
cavity. Examples and rules: [SHELL_COMMANDS.md](SHELL_COMMANDS.md).

`edge_treatment.edges` lists real input edges for fillets and chamfers.
`edge_treatment.route` returns a tangent route from a specific original edge; both
queries accept a container ID during editing. They read persisted data without
regeneration. References, endpoints, and ambiguity:
[EDGE_TREATMENT_COMMANDS.md](EDGE_TREATMENT_COMMANDS.md).

`fillet.create/get/set` supports constant and variable fillets, explicit R1 ends,
and direction reversal. `chamfer.create/get/set` supports equal/two distances,
distance plus angle, and FLIP. Commands share GUI commit and accept JSON dimensions
in mm. Route examples and R1 rules: [EDGE_TREATMENT_COMMANDS.md](EDGE_TREATMENT_COMMANDS.md).

`edge_treatment.remove` removes one edge or an entire user route using `container`,
`route` (zero-based persisted-list index), and optional original `edge` reference.
Removing the last route removes the feature in one Undo step; downstream errors
are reported explicitly with the completed deletion state.

`value_lock.list OBJECT` lists numerical field locks. `value_lock.set OBJECT KEY true`
locks a value; `false` unlocks it. Changes preserve calculated geometry and create
one Undo step, including hidden/zero values, constructions, and exact Assembly
occurrences. Valid keys, ownership, and reference addressing:
[VALUE_LOCK_COMMANDS.md](VALUE_LOCK_COMMANDS.md).

`derived_copy.sources` returns available Mirror/Pattern sources, optionally with
`object` for the boundary before an edited copy. `mirror.get OBJECT` and
`pattern.get OBJECT` read persisted source, placement, references, and parameters
without regeneration. `mirror.create/set` and `pattern.create/set` share Properties
commit. Creation requires `source`; editing requires `object`. Mirror takes a local
plane or original face reference; Pattern takes a directional grid or circular
parameters. Numerical placement uses `placement`. Explicit Assembly-copy calculation
uses the source's current calculated data even before saving it. Examples, ranges,
locks, and directional-combination counts: [DERIVED_COPY_COMMANDS.md](DERIVED_COPY_COMMANDS.md).

`component.set` edits immediate component properties using the exact `instance_path`
from `component.list/get`: name, visibility, suppression, grounding, numerical
`placement`, and complete `placement_references` (up to three plane, angle, axis,
or point mates). It shares GUI Properties commit and solver, locks, and one Undo
step. Contract, units, and examples: [COMPONENT_PROPERTY_COMMANDS.md](COMPONENT_PROPERTY_COMMANDS.md).

`component.remove` removes an occurrence from the active owning Assembly in one
Undo step using exact `instance_path`; the source file remains. Check mate and
dependency usage with `component.dependencies`. The shared GUI/CLI transaction
validates the whole candidate, including sections, before changing the live Assembly:
[COMPONENT_REMOVAL_COMMAND.md](COMPONENT_REMOVAL_COMMAND.md).

`component.activate` activates an exact `instance_path` from the displayed top-level
Assembly, whose ID can be supplied as `document`. Model commands and Save then edit
the source while the whole Assembly stays displayed. An activated subassembly uses
local paths from its own `component.list` and owns its inserted components.
`component.deactivate` returns editing to the top-level Assembly. Examples and
full/local paths: [COMPONENT_ACTIVATION_COMMANDS.md](COMPONENT_ACTIVATION_COMMANDS.md).

After activating the exact Part, `sketch.reference.refresh SKETCH_ID` refreshes
existing external references and linked native curves, trims, and offsets from
current original source data without body or mate calculation. Missing sources
remain invalid with their last geometry; another occurrence of the same Part is
rejected. Results include `body_calculated: false` and `broken_references`.
See [CONTEXT_REFERENCE_REFRESH.md](CONTEXT_REFERENCE_REFRESH.md).

### References inside profiles during regeneration

Explicit regeneration includes Sketches embedded in sweeps, helices, holes, threads,
and sections. Assembly regeneration refreshes a contextual reference in an activated
open Part even without a standalone root Sketch. Persisted original identities,
trimmed intervals, and offset dependencies are preserved. The verified CLI/native-file
workflow is in [OWNED_SKETCH_REFERENCE_REFRESH.md](OWNED_SKETCH_REFERENCE_REFRESH.md).

### Creating and detaching contextual references

After `component.activate`, `sketch.reference.create` accepts the full source
`instance_path` and supports edges, points, axes, and faces. The reference and common
Assembly dependency summary are committed together in memory. Save both the Part
and changed Assembly. `sketch.reference.delete` detaches the source while preserving
the native profile curve. GUI and Part Undo/Redo share this path. Rules, native CLI
regressions, and follow-up work: [CONTEXT_REFERENCE_TRANSACTIONS.md](CONTEXT_REFERENCE_TRANSACTIONS.md).

Assembly Undo/Redo now reconciles its derived summary with current source Part
references. `regenerate` also checks closed native owners after editing a Part
with its context closed; a changed subassembly opens unsaved. Preparation failure
leaves history in place. Details and transaction boundaries:
[ASSEMBLY_REFERENCE_SUMMARIES.md](ASSEMBLY_REFERENCE_SUMMARIES.md).

### Removing standalone constructions

`construction.delete <construction-ID>` removes a root point, axis, plane, or 3D curve
in the active Part or Assembly. It respects body ownership and rejects referenced
Assembly constructions. It creates one Undo/Redo step. Owned points and embedded
paths are edited through parent commands. See [CONSTRUCTION_REMOVAL.md](CONSTRUCTION_REMOVAL.md).

`appearance.get/set/reset/faces/palette` exposes colors and appearance. Body styles
and face groups share GUI transactions. Assembly occurrences can override appearance
or restore source inheritance without model recalculation. Scope, face identity,
and examples: [APPEARANCE_COMMANDS.md](APPEARANCE_COMMANDS.md).

`section.list/get/components` reads saved section definitions, owned Sketches,
section frames, exact occurrences, and their hatching without body calculation or
document switching. Scope and command progress: [SECTION_COMMANDS.md](SECTION_COMMANDS.md).

`section.activate` activates a section by ID; without ID it restores No Section.
`section.delete` removes a persisted definition with Undo. Both share tree-menu data
operations and preserve calculated bodies. See [SECTION_COMMANDS.md](SECTION_COMMANDS.md).

`placement.reference.remove OBJECT_ID INDEX` removes a position reference (0–2) or
FRONT/TOP orientation (3–4). It uses the same data operation as the Properties cross
button and commits the corresponding object. Missing sources are supported; one
Undo/Redo step is created. An empty row is a no-op. Supported objects, orientation
pairing, persisted indexes, and JSON examples:
[PLACEMENT_REFERENCE_REMOVAL.md](PLACEMENT_REFERENCE_REMOVAL.md).

Portable configuration layers and version selection are documented in
[NATIVE_DISTRIBUTION.md](NATIVE_DISTRIBUTION.md).
