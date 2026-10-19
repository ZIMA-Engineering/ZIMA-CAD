# Section hatching and technological thread sheets

An axial section of the DIN 1587 cap-nut illustration exposed hatch segments
crossing its empty blind bore. The solid and its bore were valid. The section
contour incorrectly included the open technological thread-runout cone along
with the closed solid boundary. Pairing those extra intersections as material
intervals generated false hatching.

Section material contours now exclude all persisted `thread:surface:` display
sheets, including runouts. Clipped technological geometry remains visible and
available through its existing references. The narrower `is_thread_surface()`
classification is unchanged: it still distinguishes threaded cylinders from
runouts. No geometry, placement, native format or reference identity changes.

Verification includes a square tube with independent open display sheets for
both runout roles and both cylinder roles. Section area remains 84 square mm;
hatch samples remain outside its cavity for four angles and three scales, while
the clipped technological triangles remain present. Existing section contracts
also cover reversed and offset cuts and save/reopen.

The cap-nut native verifier additionally samples axial-section hatching against
the actual OCCT solid classifier for both retained sides, two angles and three
scales. This checks material independently of the section polygon algorithm.
Samples just outside a concave analytic bore/chamfer must lie within the
existing native display mesher's 0.1 mm deflection, verified with OCCT minimum
distance. This is a display-triangulation bound, not a changed solid tolerance;
the tube regression checks exact cavity exclusion without this allowance.
The five localized DIN illustrations and their neutral native drawing are
regenerated with the shared implementation. No user-visible text changes were
needed; localization review covers the existing five translated illustrations.

Windows verification evidence is retained under `build/din-hatch-diagnostic`,
`build/din-cap-nut-native-section-art-v7` and
`build/din-cap-nut-art-v7-reopen`. Linux execution remains unverified.

On 2026-10-19 all 26 DIN candidates passed the independent native checks.
The section contract, drawing-hatch command, PNG/image, PDF and DXF command
tests passed (five tests). All six regenerated native drawings retained nine
resolved dimensions through regeneration and save/reopen, with unchanged
source hashes. The Czech PNG was visually inspected: the bore is unhatched.
The local Windows CLI and desktop executable were rebuilt. The first sandboxed
section test was blocked by its temporary-file atomic rename; the same test
passed outside the sandbox without changing its assertions.
