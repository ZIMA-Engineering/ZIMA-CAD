# Appearance commands

`appearance.get/set/reset/faces/palette` shares data operations with Colors and
Appearance. `set` and `reset` edit persisted styles in one history entry, without
OCCT, mate solving, or dependency regeneration.

## Scope

Without `instance_path`, the target is Part. Omitted `body` means active body;
explicit empty `body` means the document default style. Reads may target another
body ID; writing requires activation. Derived copies use explicit body IDs without
activating their geometry. Body style overrides the base style; face groups override body style.

Assembly requires `instance_path` containing exactly one immediate Part occurrence.
Edit nested Parts after activating their owning Assembly. The existing resolver maps
full displayed GUI paths to the same owner. Overrides belong only to the selected
occurrence, not the source Part or other instances. `body` may select a source-Part
body within that override. Empty scope edits the occurrence's base style while retaining
its special body/group styles.

Open source Parts are authoritative for inherited appearance too. Unsaved changes
are read directly without regeneration. Closed-source queries use the component's
persisted snapshot without opening files or tabs.

## Arguments

```json
{"command":"appearance.set","arguments":{"style":{"color":"#225FC2","roughness":0.12,"metallic":0.75}}}
{"command":"appearance.faces","arguments":{"offset":0,"limit":100}}
{"command":"appearance.set","arguments":{"groups":[{"id":"polished","name":"Polished faces","style":{"roughness":0.04,"metallic":1},"faces":[{"owner":"<container-id>","key":"<persisted-result-key>"}]}]}}
{"command":"appearance.get","arguments":{}}
{"command":"appearance.reset","arguments":{"instance_path":"<direct-occurrence-path>","inherit":true}}
```

- `style` patches `color`, `roughness`, and `metallic`. Color is `#RRGGBB` or Qt
  `#AARRGGBB`; roughness is 0.04–1 and metallic 0–1. Unknown fields/invalid numbers are rejected.
- `groups` replaces groups only within selected body scope; other groups remain.
  Empty arrays remove them. Each group has `id`, `name`, `style`, and `faces`.
  Missing ID creates a stable ID. Existing IDs may omit unchanged properties;
  omitted groups are removed.
- `faces` contains `owner`/`key` pairs from `appearance.faces`: persisted result-face
  identities used exclusively for appearance. Command `instance_path` carries occurrence context.
- `reset` removes groups/style in selected scope. `inherit:true` for a whole occurrence
  removes its override and restores source appearance. Resetting one body retains
  other occurrence styles.
- `palette` returns built-in styles with name, category, ID, and values; it does not
  edit the user's config palette.
- `get` returns effective scoped style, groups, revision, owner, and override flags.
  Mutations add `changed`.
- `faces` accepts `offset >= 0` and `limit` 1–10000, default 2000, returning sorted
  unique pairs and total count.

## Shared commit and GUI

`prepare_appearance_edit` captures owner and original settings. `commit_appearance`
validates revision, open-document identity, scope, styles, and newly assigned faces.
Stale proposals are rejected before mutation. Previously stored missing faces survive
unrelated style edits; new nonexistent faces cannot be assigned.

GUI preview is transient. OK uses shared commit; Cancel restores original display.
Color edits preserve exact roughness/metallic values unless their sliders changed.
Console emits a separate appearance-change notification to refresh View styles without
unnecessarily rebuilding the scene.

The user palette file is written only when its contents change. Selected style and
all groups are fully persisted in `.prtz`/`.asmz`; reopening requires no palette file.
Formats/start templates are unchanged. All new text has five config-language translations.

## Verification

Baseline failed on missing `appearance.faces` (0/1, 0.12 s). Initial model/catalog
tests passed 2/2 in 0.53 s. Regressions cover six calculated box result faces, unchanged
volume, queries preserving cache allocations, groups, atomic rejection, history,
separate instances, and native saving. Extensions add stale proposals, inactive bodies,
unsaved-source inheritance, actual CLI, and GUI OK/Cancel with precision preservation.

A separate regression exposed missing result-face owner mapping for mirrored bodies
(0/1). Mapping now includes derived bodies and is shared by rendering, source insertion,
and Assembly refresh. A later coloring assertion incorrectly expected the source
Body active even though Mirror creation ends activation. The repaired test compares
state immediately before/after coloring and separately verifies geometry-snapshot sharing.
Extended model tests passed **1/1 in 0.20 s**, including closed source Part:
`build/appearance-scope-tests.log`. After both applications and tests built, full
**122/122 passed in 531.19 s**, including models, actual CLI, GUI console (55.43 s),
appearance visual contract, Assemblies, startup, and translations.
Log: `build/appearance-full-tests.log`. Catalog: **218 commands** at this stage.
