# Assembly

## Before you start

Prepare native Parts or subassemblies, then create an **Assembly** (`.asmz`).
Each inserted occurrence has its own identity and placement.

## Steps

1. Use **Insert** to add a Part or nested Assembly. Review its occurrence in
   the Tree.
2. Open the occurrence's **Properties**. Arm a component reference field,
   select its geometry, then select the corresponding owning-Assembly reference.
3. Choose the offered mate and value. Add independent references until the
   intended translational and rotational freedoms remain.
4. Inspect the placement and confirm **OK**. **Cancel** restores the pending
   changes, including dependent placements.
5. Use **Active** to edit a component's source. A Part exposes Modeling tools;
   a subassembly exposes its own Assembly tools. Use **Back to Assembly** to return.

## What to check

Only the immediate owning Assembly positions a component. Activate a subassembly
before editing its internal placements. The top-level scene stays visible as
context, and external geometry is read-only reference material.

Current calculated Part geometry appears in its occurrences. **Regenerate**
explicitly solves mates and Assembly-owned operations; switching tabs does not.
New material features are authored in Parts, not at Assembly level.

See [Assembly workflow](../USER_MANUAL.md#basic-assembly-workflow) and
[Assembly references](../ASSEMBLY_REFERENCES.md).
