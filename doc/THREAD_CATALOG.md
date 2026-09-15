# Shared thread catalog

`thread.catalog` reads the same data as Opening and External Thread Properties.
It requires no open document, calculates no geometry, and changes no history or Undo.

```text
thread.catalog metric M10
```

```json
{"command":"thread.catalog","arguments":{"standard":"whitworth","designation":"W 1/2"}}
{"command":"thread.catalog","arguments":{"standard":"metric","offset":100,"limit":100}}
```

Required `standard` is `metric`, `whitworth`, or `pipe`. Optional `designation`
matches the exact stored name; an unknown name returns empty `items`. Fine metric
threads use `×`, such as `M10×1`. Decimal commas in some names remain part of the
original table designation.

`offset` is an integer from 0 to 100000000, default 0. `limit` is an integer from
1 to 1000, default 100. Results contain `standard`, `items`, `total`, `more`, and
`next_offset`. `total` counts filtered entries. A page beyond the table is empty
with `more: false`.

Each item contains:

- `designation`: original designation;
- `nominal_diameter_mm`: nominal diameter;
- `pitch_mm`: pitch;
- `internal_root_diameter_mm`: tabulated minor diameter of the internal thread;
- `external_root_diameter_mm`: tabulated minor diameter of the external thread;
- `preferred`: the same common-size emphasis as GUI.

All numerical dimensions are millimeters, including inch designations. `W 1/2`,
for example, has nominal diameter 12.7 mm. According to the existing table,
`G 1/2` has nominal diameter 20.955 mm; the pipe designation does not imply 12.7 mm.

## Source and verification

`document_core` now loads immutable catalogs without Qt. CMake embeds the same
versioned TSV files under `resources/data/threads`; running applications do not
search for external tables. GUI only converts text to QString. Original catalog
values and rules remain unchanged, including deriving G pitch from tabulated
diameters. This unifies existing data; it is not a new thread-standard audit.

The native test checks all 392 metric, 28 Whitworth, and 24 pipe entries, unique
names, dimension ranges, and specific M10, M10×1, W 1/2, and G 1/2 entries. The host
tests pagination, lookup, and invalid-argument rejection. The process test runs
actual CLI outside the project; GUI checks adopting M10 in the dialog and retaining
a custom diameter when editing another dimension.

Opening/thread creation and editing are the subsequent stage. This read-only
command changes neither native document formats nor start templates.

Both programs and tests built (`build/thread-catalog-full-build.log`). Related tests
passed **8/8** (62.70 s), `build/thread-catalog-related-tests.log`. After removing
duplicate Qt table packaging and adding GUI queries and invalid-page checks, the
final build and **5/5** tests passed (61.14 s):
`build/thread-catalog-final-build.log`, `build/thread-catalog-final-tests.log`.
