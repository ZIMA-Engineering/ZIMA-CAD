# Sheet material regression documents

- `box-cross-branch.prtz` exercises a branched Sheet Profile box and shared
  references during Unbend/Bend Back.
- `tilted-cone-with-bends.prtz` reproduces the reported `04.prtz` case: a Flat,
  two Sheet Profiles, Sheet Cut, an attached Revolved Sheet with an inclined
  axis. Tests append Unbend All and Bend Back. Calculated body caches were removed; native authored
  history, ancestry and references were preserved. The cone previously unfolded
  alone but failed when unrelated bend partition planes split its material.
  Tests read this fixture or work on temporary copies, never save into it.

The cone and profile-side-twist fixtures explicitly mark original point
references with `body_edge=false` (2026-09-23). This updates the authored test
data to the current reference schema; no legacy loader fallback is required.
