# Cylindrical Fillet terminals and shaded rollback input

## Reported behavior and scope

A ZIMA Fillet on a circular rib edge stopped with perpendicular ends at
tangent cylindrical bosses. The corresponding OCCT comparison terminated on
the cylinders. Separately, reopening Fillet Properties after a Hole displayed
only the Hole faces shaded and left the preceding features as wires.

Investigation used isolated copies of the reported Part. Original user files
were not modified. The cylinder snapshot SHA-256 is
`50cf1a2a8a9e27aa27e0963c857c76c922adccfb40767372bb8fd3ac6d062f3c`.
Private models and screenshots remain in ignored local verification folders.

## Changes

- A material probe can permit continuation through an earlier curved face,
  but continuation now requires an actual adjoining terminal plane. If that
  search finds no plane, the local curved surface remains the exact stop.
  Existing continuation with a proven terminal plane uses the same path.
- The lightweight circular-route preview intersects parallel cylindrical
  terminal surfaces analytically and trims its existing rails/end strokes.
  It uses the shared authored-wire generator and performs no Body calculation.
- Explicit history calculation retains the complete immediate input packet
  for an active Fillet or Chamfer. A compact dependency packet containing
  only requested Hole walls is insufficient for the shaded rollback scene.
  Properties reads this retained input; it does not rebuild the final Body.

There is no document-format, placement, side-identity, OCCT-version or
platform-specific algorithm change. No user-visible text was introduced or
changed; the five-language translation contract passed.

## Verification

| Check | Coverage and result |
| --- | --- |
| Independent native fixture | A 10 mm rib, R30 circular edge and two R20 cylindrical bosses. R1/R3/R5, both edge orientations: valid single solid, closed shells, nonzero rib removal, no removed volume inside either boss, and a result distinct from perpendicular NoCut. |
| Baseline rejection | The same new fixture linked against the original kernel source fails at R1 because the cutter removes boss material. |
| Independent wire equations | R1/R3/R5, both route orders and all four independent Cut/NoCut endpoint combinations (24 cases). Cut samples satisfy the cylinder equation; NoCut samples retain their perpendicular section. No Body calculations. |
| Reported circular rib | Fresh calculation and native save/reopen, R1/R3/R4/R5/R6/R8. Every authored preview sample lies on an exact result boundary, maximum measured deviation below 5e-8 mm. Preview preparation measured about 0.15–0.23 ms in this run, with zero Body calculations. |
| Actual GUI preview | Shared four-stroke preview and source labels match the published Properties overlay; Cancel preserves the document. Standard-view screenshots were inspected. |
| Hole rollback GUI | Four repeated unchanged Cancel/OK cycles preserve the complete shaded input, display mode and document revision, with zero Body calculations. R3/R5/R4 edits, Undo/Redo and save/close/reopen preserve route/end identities and complete shaded input. |
| Existing native Fillet suite | Convex/concave, branch nodes, mixed CUT/FUSE, pocket/notch terminals, circular and B-spline paths, preceding-radius and tangent ends, rolling supports and ancestry/rejection checks passed. |
| Related native suites | Planar Chamfer, edge-treatment supports, direct batches, edge queries and history dependency layers passed. |
| Existing wire suite | Planar/circular terminal planes, remote ends, rolling supports and independent Fillet node wires passed. |
| Command lifecycle | Existing core command and node checks passed separately: sizes, volumes, ownership, locks, rollback, native files and Undo/Redo. The unchanged test functions were invoked from an isolated local driver. |
| GUI creation | Existing creation grip and transaction checks passed. |
| Localization | `zima_cpp_translations_contract` passed for the existing five-language catalogs. |

The original R4 cached result intentionally predates this fix. Comparing a
new preview with that unchanged cached result fails; explicitly regenerating
the isolated copy supplies the corrected result. Opening unchanged Properties
must not silently regenerate old calculated geometry.

The full command executable also exercises a separate solid-to-sheet
conversion. That check stops on the symmetric upper-cap fixture with
`Flat profile conflicts with its sheet endpoint references.` Rebuilding all
three changed production translation units from the original commit reproduces
the same failure. The remaining command functions were therefore run separately,
without disabling or weakening the failing assertion in the repository.

The user identified the closed-skin opening/Rip requirement in this fixture:
where rounding removes a sharp corner, an automatic Rip needs a location derived
from the arc center/middle instead. The existing converter has visited-face
seam handling and explicit Rip-sketch inputs, but this failure's precise cause
is not established. Automatic rounded-corner Rip design and implementation are
outside this Fillet fix; the geometric meaning of the proposed arc reference
must be resolved before implementation.

## Limits and delivery

This is finite Windows verification with the shared C++ implementation and
OCCT 8.0.1. Native Linux execution was not performed. The new analytic preview
case covers circular routes and parallel cylindrical terminals, not arbitrary
freeform terminal surfaces. The added rollback GUI fixture covers a Part,
not nested Assembly editing.

These follow-up changes are not included in the immutable public
`ZIMA-CAD-2026112303` package. After the user saved and closed the application,
the GUI and CLI from commit `222825f2af7112f84fc0581657044ec0ebf417b2`
were installed in the existing local development build directory. Both report
that development source identity. The unchanged `zima-cad.bat` still launches
the canonical GUI path. Previous binaries were backed up; launcher and user
configuration hashes were unchanged. The installed GUI passed the Hole
rollback/edit/native-reopen check again.

Local installation evidence is retained in the ignored
`build/fillet-cylinder-end-20261123/local-installation.json`. This development
installation is separate from publication of the verified 2026112303 package.
