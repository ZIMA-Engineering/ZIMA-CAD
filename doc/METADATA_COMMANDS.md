# Document parameters and settings through GUI and CLI

`document.parameters.get/set` and `document.settings.get/set` share
`workspace::metadata_operations` with Parameters and File Settings dialogs.
Creating/removing/reordering parameters replaces the whole ordered table; settings
accept a partial patch.

## Parameters

```json
{"command":"document.parameters.get","arguments":{}}
{"command":"document.parameters.set","arguments":{"parameters":[
  {"key":"NUMBER","values":{"":"ZE-100"},"labels":{"cs":"Číslo","en":"Number"}},
  {"key":"NAME","values":{"cs":"Držák","en":"Bracket"}}
]}}
```

The example retains Czech values as literal multilingual data. Array order is dialog
row order. `key` is unique, nonempty, trimmed, without control characters, at most
256 UTF-8 bytes. `labels`/`values` map language keys to text; omitted objects are empty.
An empty language key in `values` means a shared value usable by relations. Language
variants remain separate and do not create shared numerical parameters themselves.

Limits: 4096 parameters, 128 language variants per item, 65536 bytes per text value.
The overall CLI line limit remains 64 KiB. Unknown fields/wrong types are rejected,
never silently omitted.

Existing physical relations remain authoritative for their targets. For example,
start-template `mass = model.mass` rederives its parameter from stored physical data
after commit even if omitted from the supplied table. Results return the actual
final table including derived rows. Repeating identical requests adds no Undo.
Relation editing belongs to a subsequent command stage.

## Units and precision

```json
{"command":"document.settings.get","arguments":{}}
{"command":"document.settings.set","arguments":{"units":{"Length":"cm"},"precision":{"mesh_deflection":2,"decimal_places":6}}}
```

`get` returns `units`, numerical `precision`, and `unit_choices`. Commands/dialogs
share choices: Length mm/cm/m/in, Angle deg/rad, Mass kg/g/t/lb, Time s/min,
Temperature C/K/F, Stress Pa/kPa/MPa/GPa/psi. Unknown quantities/units fail before mutation.

`linear_tolerance` and `angular_tolerance` accept finite 0–1000000;
`mesh_deflection` 0.000000001–1000000; `decimal_places` integer 0–12. Linear tolerance
and mesh deviation are mm. `angular_tolerance` remains a stored existing setting;
this stage introduces no new kernel use. Unit/decimal changes do not recalculate
geometry. Linear-tolerance/mesh-deviation changes compare calculation requests;
if different, OK or the command explicitly recalculates the affected Part through
shared solving. Assembly recalculates only its owned sections, if present, without
refreshing sources/parents. Imported-feature precision overrides still take precedence.

This is also required for consistent saving: native files validate cache against
parameters and precision. The old dialog could leave stale cache fingerprints and
cause Save failure. Commit now stores settings and corresponding results in one Undo
step. Failed calculation preserves the document. Formats and container placement are unchanged.

Changing mm to cm converts displayed physical values: 6000 mm³ geometry remains
unchanged and displays as 6 cm³. Units/decimal changes refresh related physical
relations from calculated measures without OCCT.

## Transactions and interface

All four commands accept optional `document`. Queries may target open Part/Assembly;
mutations require the active document under ordinary guards. GUI Drawing-source
parameters retain the original explicit opening workflow; CLI opens/activates the
source separately.

Results contain `document`, `revision`, and mutation `changed`; settings additionally
return `calculated`. The full request and dependent physical relations are validated
before live mutation. Errors such as division by zero preserve data generation too.
Actual changes create one Undo step; identical final data creates none. Ordinary
metadata preserves calculated B-Rep and shared Assembly component snapshots; parents
are not refreshed.

GUI retains PropertiesSubWindow with OK/Cancel. Cancel writes nothing; failed commit
keeps pending values for correction. Decimal-count changes update the window property
identically through dialog or console. `.prtz`/`.asmz` saving remains explicit.

## Verification

Model regression covers language variants, ordering, derived `mass`, identical data,
Undo/Redo, invalid types/units, relation errors, 6000 mm³ → 6 cm³ conversion without
B-Rep change, required precision-triggered calculation, preserved Assembly/parent
snapshots, and native saving. Actual CLI saves/reopens files. GUI reads CLI values
in dialogs, edits through OK, verifies Cancel, and changes units/precision with real controls.

The catalog has 112 commands at this stage. Full Windows Release passed **74/74**
(405.58 s); `build/metadata-full-tests.log`.

Parameter/material values and descriptions are single-line without surrounding
spaces/tabs to survive native INI serialization. Keys cannot contain backslashes,
commas, `=`, `[`, or `]`, or begin with `#`/`;`. Multiline values and reserved separators
fail before transactions. Family-table cells are inside JSON and do not have this
single-line restriction.
