# FEM condition entry and projected-curve meshing, 2026-11-24

## Changes

The FEM dialog no longer prepares an already prepared placement reference a
second time. That second preparation consumed the measured offset before the
shared section could assign it to a locked empty row. The shared placement
solver, side conventions and reference-assignment implementation are unchanged.

Submission exceptions now reach the shared Properties error row above OK/Cancel.
Previously the dialog caught them into a status label inside its scrollable form,
where a user could see no response to OK. Successful conditions still use the
existing metadata transaction and unchanged OK remains a no-op.

Committed projected FEM regions display their projected curves and points only.
The authored source Sketch remains visible while editing. This is a FEM-local
presentation filter; the shared Projection preview retains its existing behavior.

The volume mesher now fragments projected curves with the private mesh input,
using the existing batch partition and condition-group mapping. Plain embedding
could leave independent endpoints where a projected curve met a face boundary
or another condition. No CAD source, geometric tolerance, element order or mesh
size is changed to obtain success. This follows Gmsh's documented distinction
between [embedding and conformal fragments](https://gmsh.info/doc/texinfo/gmsh.html#gmsh_002fmodel_002fmesh_002fembed).

## Reproduction and verification

An isolated copy of the user's `50.prtz` contains a projected Curve constraint,
a body-surface constraint and a point Force. At minimum size 0.01 mm and maximum
size 5 mm, the original mesher reports `Impossible to recover the edge 64 65`.
The solid without conditions passes. The projected Curve alone reproduces the
failure; reducing maximum size to 2 mm still fails (edge 172 173). Each remaining
condition passes separately.

With the repair, all three conditions pass together at the original sizes:
32,720 nodes, 18,382 quadratic tetrahedra, and three condition groups. This run
took 22.8 seconds on the local Windows host; it is a reproduction observation,
not a general performance claim. The existing high-order Jacobian validation
and any required optimization remain active.

- Native FEM condition suite: 96 owned-Sketch dimension/edit/drag combinations,
  native persistence, Undo/Redo, metadata-only changes and unchanged source pass.
- Native pressure/mesher suite: existing box, planar/projected area, cylinder,
  load balance and stress checks pass. New cases cover projected curves across
  a cylindrical seam, on face boundaries, and crossing another projected curve,
  for linear and quadratic elements. Their condition nodes belong to volume
  elements, intersections share exactly two nodes, and the source BREP is unchanged.
- Actual GUI placement now tests a locked second Origin-plane row for all three
  first-plane orders. The full FEM GUI suite passes, including five languages,
  two themes, forces, constraints, pressure, thermal and combined analyses,
  interaction, Cancel, Undo/Redo, cache reuse and native reopen.
- The interaction-color GUI suite checks Constraint, Force and Pressure region
  wires: four projected point grips after confirmation, eight source/result
  grips during editing, correct target coordinates and preserved authored data.
- A further GUI regression requires a failed off-body Force submission to show
  the shared error row without changing the document revision.
- Planar Curve and Surface Forces pass four successive FRONT/BACK switches
  using the real shared button, OK and Undo/Redo. The signed local plane offset
  retains the same intended world plane; its origin and reversed normal are
  checked independently. The full FEM GUI suite passes after this addition.

## Explicit second-OK retention

The user approved the FEM native-data extension on 2026-11-24. A condition may
persist an optional `calculation_error` inside its existing native definition.
Valid conditions omit it; current Part format identifiers and existing valid
records do not change. No sidecar or conversion adapter is introduced.

FEM uses the existing shared modeling confirmation mechanism. A failed geometric
evaluation shows the existing translated error beside OK/Cancel and leaves the
document unchanged. A second identical OK retains the definition and diagnostic
in one metadata transaction, without repeating the failed evaluation. Changing
the draft or any captured source generation invalidates the acknowledgement.
Cancel discards the pending submission. Structural validation remains mandatory:
this does not authorize malformed records, nonfinite values or missing required
fields to enter the native format.

Retained failures are red in the Tree with an error tooltip and marked broken in
the View. Meshing and the applicable mechanical/thermal analysis reject them;
they cannot reuse a prior successful result. Reopening Properties and successfully
evaluating a repaired definition clears the error. Last valid placement and
authored Sketch/reference data remain independent of this diagnostic.

Native checks cover Constraint/Force/Pressure with both planar and projected
regions: first failure, changed draft/source, second confirmation, save/reopen,
explicit regeneration, Undo/Redo, repair and source-snapshot reuse. Confirmation
and repair perform no CAD Body calculation; the second unchanged acknowledgement
does not repeat geometric validation.
The ordinary Fillet/Chamfer shared-confirmation suite also passes. The new
calculation-blocking message is translated in all five supported languages;
existing shared first/second-OK instructions are reused. Catalog validation
covers the concurrent Render/Section labels as well.

The full FEM GUI suite passes with second-OK retention through both the actual
button and View middle-button double-click. It checks the red Tree/tooltip,
meshing and solving rejection, Undo/Redo, reopening for repair, subsequent
unchanged OK and repeated FRONT/BACK edits. Mechanical, thermal and combined
analysis regressions, five-language messages and two themes also pass.

## Factory and protected-project verification

All six ordinary Part/Skeleton/Assembly templates were saved and reopened with
the changed native serializer; both tool start templates were recalculated,
saved and reopened. Their bytes remain unchanged. Actual GUI template creation
passes, including the first editable context and normal commands. The 1,190
tracked native factory assets have no FEM Body definition requiring conversion.

The protected Toyota project was backed up in full: 172 files, 2,603,459,762
uncompressed bytes. Every backup member and working copy matches its source
SHA-256; the recoverable ZIP SHA-256 is
`62c71329d229322fdf0c5b3e086604b65cd2957183887d7f711cb6617b481ccd`.
The backup and inventory remain under `.local-backups/toyota-20261124/` and are
not release inputs. All original hashes were rechecked after testing.

Both Drawing copies pass native save/reopen. Seven Parts and one Assembly are
already unsupported by the exact CLI binary from signed Windows 2026112303;
the changed runtime rejects the same files. Their modeling/reference behavior
remains unverified. No conversion, version-field substitution or write to the
original project was performed.

The user's earlier unsaved projected-area Force definition was not available in
the saved reproduction, so its exact original validation failure is not proven.
Native Linux execution remains unverified.

Evidence: `build/fem-20261124/`, using private inputs only. These results describe
the local candidate; publication requires the ordinary Windows release gates.
