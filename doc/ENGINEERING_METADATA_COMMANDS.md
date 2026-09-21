# Relations, material, and family tables through shared commands

GUI console and standalone CLI share existing Relations, Material, and Family Table
commit operations. Relations and Family Table support open Parts/Assemblies;
Material supports Parts only, including a Part activated within an Assembly.
Changes create one Undo transaction;
identical final values create none. Reads, relation evaluation, and material edits
use persisted physical values without B-Rep calculation or parent-Assembly refresh.

## Commands

| Command | Arguments | Purpose |
| --- | --- | --- |
| `document.relations.get` | `[document]` | Ordered relations, user parameters, available physical values, precision |
| `document.relations.set` | `relations:Array`, `[document]` | Replace/evaluate all relations |
| `document.material.get` | `[document]` | Properties, units, language descriptions, unit choices |
| `document.material.load` | `path`, `[document]` | Load/assign `.matz` |
| `document.material.set` | `properties:Array`, `[document]` | Replace material data |
| `document.family.get` | `[document]` | Persisted family table |
| `document.family.set` | `table:Object`, `[document]` | Replace reference-bound family table |
| `document.family.references` | `[document]` | Original owners, dimension identifiers and base values |
| `document.family.open` | `instance`, `[document]` | Explicitly calculate/open the named variant |

Optional `document` identifies an open document. At this original stage, mutations
require the active standalone document and reject pending GUI dialogs or nested
activation. Results contain `document`, `revision`, and mutation `changed`.
Persist `.prtz`/`.asmz` explicitly with `save`.

```json
{"command":"document.material.set","arguments":{"properties":[{"key":"MATERIAL_NAME","value":"Aluminum","descriptions":{"en":"Material name"}},{"key":"MASS_DENSITY","value":"2700","unit":"kg/m^3"}]}}
{"command":"document.relations.set","arguments":{"relations":[{"target":"grams","expression":"model.mass * 1000"},{"target":"double_grams","expression":"grams * 2"}]}}
{"command":"document.family.references","arguments":{}}
```

Relations evaluate in order using unrounded intermediate results. They support the
current native arithmetic/function language, not Python or shell. Targets are unique
ASCII identifiers. Available names appear in `model_values`; unavailable mass is not
invented. Removing a relation retains its target parameter's last value. Empty lists
remove all relations; opening an empty dialog creates none. Relations currently
drive user parameters, not modeling-feature dimensions.

Material rows have textual `key`, `value`, optional `unit`, and language-keyed
`descriptions`. Numerical density must be finite/positive and supports `kg/mm^3`,
`kg/m^3`, `g/cm^3`, and `lb/in^3`. Other unit choices depend on the property as in GUI.
Assemblies do not own material fields in their native files or runtime document
model. Material get/set/load requests for Assemblies are rejected without changing
history. The Assembly Tree retains a disabled Material icon; activating a Part
enables it for that Part. The regenerated Assembly start template also omits the
predefined `material` user parameter. General user-defined parameters remain
available. Assembly mass derives from component snapshots.
At this original stage source updates were
explicitly regenerated; current source sharing is documented in
[ASSEMBLY_GEOMETRY_SHARING.md](ASSEMBLY_GEOMETRY_SHARING.md).

The Part-only material change is verified by native Assembly serialization,
rejected Assembly material commands with unchanged history, component mass,
Part material editing, activation of a nested Part, and new-document creation
from the regenerated Assembly start template in all five UI languages.
All six selected contracts pass in `build/part-only-material-tests.log`.

Family Table stores original model references and dimension/presence overrides.
`document.family.set` commits the shared table and updates evaluated variants;
`document.family.open` explicitly opens a linked Part or Assembly row. Name-only
changes reuse calculated geometry. Ordinary Save persists the family in one file;
Save As creates an independent copy.
See [Family Table](FAMILY_TABLE.md) for the current schema, GUI workflow, scope,
instance refresh rules and Drawing support.

Validation uses a private copy. Division by zero, unknown names, invalid units,
duplicates, or malformed structures preserve revision and data generation. The parser
limits recursion through parentheses, unary operators, and powers, including callers
outside the console. Limits: 4096 relations, 16384 expression bytes, 256 active recursive
rule entries, 4096 material properties, 512 columns, and 4096 variants. Invalid GUI
OK retains pending values in the same internal dialog; Cancel writes nothing.

## Verification

Independent check: a 10 × 20 × 30 mm box at 2700 kg/m³ weighs 16.2 g. Model tests
cover chained relations, B-Rep preservation, Undo/Redo, atomic errors, long recursive
expressions, multilingual descriptions, table normalization, Part/Assembly saving,
and no parent refresh. Actual CLI saves/reopens native files. GUI commits the same
data through original dialogs, including invalid OK and Cancel. Focused tests passed
**5/5** (16.77 s), `build/engineering-metadata-tests.log`. Catalog: **118 commands**.
Full Windows Release passed **75/75** (406.56 s), `build/engineering-metadata-full-tests.log`.
After single-line validation, final model/CLI/GUI tests passed **6/6** (24.23 s),
`build/engineering-metadata-final-tests.log`.

Parameter/material values and descriptions are single-line without surrounding
spaces/tabs for native INI persistence. Keys cannot contain backslashes, commas,
`=`, `[`, or `]`, or begin with `#`/`;`. Multiline values/reserved separators fail
before transactions. Family-table cells are JSON and have no such line restriction.

## Material library

`document.material.load` reads native `.matz` and commits the same data as dialog OK.
Relative paths use CLI working directory; results add absolute `source`. Documents
store complete assigned material, so the original file may later move or disappear.
Mass and physical relations use persisted volume without OCCT or parent refresh.

```json
{"command":"document.material.load","arguments":{"path":"Steel.matz"}}
```

GUI/CLI share a Qt-free reader for current UTF-8 INI sections `Material`, `Properties`,
`PropertyUnits`, and `ParameterDescriptions`, accepting CRLF and optional BOM. Values
are plain text; embedded `=`, `;`, and `#` remain literal. Lines starting with `;` or
`#` are comments. A language-description key can be `MASS_DENSITY\cs`.
Libraries are limited to 16 MiB. Duplicate keys/sections, unknown sections, invalid
UTF-8, missing name, forbidden units, or invalid density fail before document or
pending GUI-table changes.

GUI Load from Library edits only pending data; OK commits and Cancel discards.
Reassigning identical data creates no unnecessary Undo step.

Verification covers all **62** supplied materials, Czech paths, descriptions, BOM,
literal text, and invalid files. Independent S235JR check: 6000 mm³ at
7.85·10⁻⁶ kg/mm³ gives 47.1 g. Actual CLI saves, deletes the source library, and
reopens. GUI performs real file selection and confirmation/Cancel. Final tests
passed **6/6** (25.05 s), `build/material-library-integration-tests.log`.
Catalog: **119 commands** at this stage.
