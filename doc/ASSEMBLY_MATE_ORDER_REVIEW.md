# Fixed: component mate evaluation order

## Verified defect

With tree order A, B, C, A has a plane mate to B with a 2 mm offset and B to C
with a 3 mm offset. C is at z = 0. After confirming B's mate, B reaches 3 mm,
but A remains at 2 mm instead of the independently calculated 5 mm.

`AssemblyDocument::calculate_placement_references` made one pass in persisted
component order. A used B's previous position, which changed later in that pass.
GUI preview, GUI confirmation, CLI, and explicit regeneration share this function.

The reproduction extension to `cpp/tests/component_property_command_tests.cpp`
is preserved in stash `953168c8669d7391df429da82c4b187454e3f028`
(`codex pending approval: assembly mate evaluation order`).
`build/component-chain-repro-tests.log`: **0/1**, expected 5 mm, actual 2 mm.
The baseline CLI stage is commit `efbfc58`; its full 107/107 suite and final
16/16 suite passed before this new scenario.

## Approved and implemented change

Only `calculate_placement_references` in
`cpp/modules/assembly/src/assembly_document.cpp` changes its direct traversal:

1. Map existing occurrence IDs to their positions in the persisted list.
2. Collect immediate target components from each ungrounded component's
   `target_reference.instance_path`, counting duplicate targets once. An Assembly
   reference with an empty path has no predecessor. Missing occurrences introduce
   no geometry; existing invalid-reference handling remains.
3. Process components without predecessors first, then their followers. Use the
   existing `make_placement_system` and `solve_placement` with updated targets.
4. Reject the entire calculation if a cycle remains. Work uses a private
   candidate; positions are published to the live document only on success.
5. Preserve component order, identities, references, equations, locks, appearance,
   source packets, and document format. Introduce no OCCT calls.

An iterative queue replaces recursion or repeated regeneration. The change also
affects Properties previews, dragging, and regeneration of all Assembly mates,
so this shared change required specific approval.

## Verification

- The A/B/C chain must start at 5/3/0 mm and reach 15/13/10 mm when C moves to 10 mm.
- One Undo/Redo of C's change restores/reapplies the entire chain.
- All six orders of three components produce identical geometry without tree reordering.
- Native calculation rejects cycles without publishing partial positions.
- Existing model, CLI-process, and actual GUI tests cover mates and derived copies.

The user explicitly approved this repair on 2026-09-13 after its effects on
shared placement were explained. This resolved the earlier block for this
specific repair. The native solver now uses a dependency queue on a private
candidate and rejects cycles before publishing results.

The current reproduction first failed: expected 5 mm, actual 2 mm
(`build/mate-order-baseline-tests.log`, 0/1 in 0.28 s). After the repair, model
tests passed **2/2 in 0.88 s** (`build/mate-order-first-tests.log`). Alongside the
chain and six orders, they cover duplicate targets, preserved tree order and
shared geometry, repeated calculation, explicit regeneration, native saving,
atomic cycle rejection, and the original grounding rule.

Both applications and all test programs built. Integration passed **9/9 in
34.98 s** (`build/mate-order-integration-tests.log`): Assembly, components,
nested activation, placement, derived copies including GUI, and the actual CLI
process. Subsequent GUI tests passed **3/3 in 117.99 s**
(`build/mate-order-ui-tests.log`): component properties, startup and translations,
and Assembly display updates. The new cycle error has all five translations.

The repair changes evaluation order for existing Assembly mates. Equations,
locks, identities, persisted tree order, and document formats remain unchanged.
Switching tabs introduces no OCCT work. Separate derived-copy calculation is unchanged.
