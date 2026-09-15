# Removing standalone constructions

`construction.delete` accepts `construction` as a stable standalone construction
container ID and optional active `document`. It does not accept point order, object
name, or displayed-entity ID. It uses `workspace::delete_construction`, shared with
GUI tree removal.

```json
{"command":"construction.delete","arguments":{"construction":"<construction-id>"}}
```

Results contain `document`, `construction`, `removed`, `changed`, `revision`, and
`body_calculated`. Part results also include downstream calculation errors when a
calculated boundary exists. Failed validation removes nothing. Success creates one
shared Undo/Redo step and persists in existing `.prtz` or `.asmz`.

## Ownership and references

- Part uses existing history deletion: preserve active-body rules, reevaluate the
  model, and mark unavailable downstream geometry as before. This does not change
  Part rules for deleting used features. If a later feature cannot calculate,
  preceding valid geometry remains available under the ordinary history contract.
- Assembly checks whether another persisted placement, construction, Sketch, or
  section uses the construction or its owned points, entities, and Origins.
  This includes component mates and Assembly-cut placement/targets. The same ID on
  a different occurrence path is not a dependency on the local construction.
- External Sketch checks in open Parts include embedded profiles and section Sketches.
  Traversal borrows states instead of copying all open geometry and history. It is
  not an index of arbitrary documents on disk.
- Edit owned 3D-curve points through the parent's complete point list and embedded
  Sweep paths through the owning Sweep command. This command rejects children
  instead of silently deleting their entire parent.

Assembly retains existing subsequent construction and component-placement solving.
Dependency checks invoke no OCCT. Placement solver, source identities, serialization,
and templates are unchanged.

## Errors

`construction_not_found` means unknown ID; `owned_construction` and
`embedded_construction` protect owned points and paths. `construction_in_use`
protects used Assembly constructions. Part retains `inactive_body` and
`read_only_body`. General host guards reject edits to inactive documents, Drawings,
and open pending dialogs.

## Verification

The baseline model test confirmed a missing command (`unknown_command`),
`build/construction-removal-baseline-tests.log`. Coverage includes:

- Part: 24 mm³ box volume, history ownership, native saving, Undo/Redo.
- Rejection of 3D-curve-owned points, embedded paths, and inactive bodies.
- Assembly: references from another point, curve point, Sketch, section, cut
  properties/targets, component mates, and external Part profiles/sections.
- Preserved revision, geometry, and history on rejection; distinct foreign paths.
- Actual CLI saving Part/Assembly and repeating Undo/Redo.
- GUI tree menu and console removal of the same objects.
- All five localizations of new text.

Both applications and all test programs built
(`build/construction-removal-integration-build.log`). Integration passed **9/10 tests
in 168.86 s**, including actual CLI (20.43 s), console/tree menu (51.81 s), and full
startup GUI/translations (94.71 s). The remaining new test exposed a fixture error:
it activated another Part while leaving the original displayed, so the general guard
correctly rejected mismatched context before checking the embedded path.

After correcting only that fixture, **4/4 focused regressions passed in 0.89 s**
(`build/construction-removal-final-tests.log`), including all nine used-construction
scenarios. Production code was unchanged after integration. All relevant model,
process, and GUI scenarios therefore passed. The catalog grows from 209 to 210 commands.
