# Shell Properties opening - 2026-10-01

## Inputs, means and output

The inputs are a calculated Part, stored Shell thickness and opening-face
references. The existing rollback refresh supplies the actual input body used
by Shell face selection. The required output is the same editable input scene,
reference behavior and correct model after OK or Cancel.

Baseline: `46716d15`. Tree dispatch published the final body immediately before
the Shell editor published its rollback input. Shell now joins the guarded
command-owned refresh path. Its own scene refresh, face table, candidate filter,
reference highlights and finish handlers remain unchanged. No shared placement,
geometry-side, kernel, precision, file-format or transaction logic changes.

## Measurement and equivalence

The existing treatment opening probe also supports `ZIMA_VERIFY_SHELL_OPENING=1`
with `ZIMA_VERIFY_CONSOLE_ONLY=1` and `ZIMA_VERIFY_EDGE_TREATMENT_ONLY=1`.
It creates native Sketch-based 4- and 128-sided prisms, circumradius 100 mm and
height 40 mm, with a closed 0.5 mm Shell. Explicit model calculations and saves
occur before timing. These are synthetic fixtures, not user-document timings.

Serial Windows Release/Fusion, 1500 x 950. The pre-existing first Shell opening
changes camera framing. An untimed open/Cancel establishes that frame in both
versions; this change does not fix or claim to remove that behavior. The cursor
is placed outside View before opening, so incidental face hover cannot change
the frame comparison. Six subsequent openings use three unchanged OK and three
Cancel. The means below omit the first measured sample. Frame hashing and image
capture are outside the opening timer.

| Fixture | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| 4-sided prism | 36.041 ms | 34.921 ms | 3.1% |
| 128-sided prism | 147.306 ms | 84.023 ms | 43.0% |

Each measured opening publishes one base scene instead of two. The large-fixture
reduction is about 43%; the small 3% difference is not a reliable general speedup.
All 12 corresponding preview and 12 restored framebuffer hashes match exactly.
Every cycle checks exact viewer-packet restoration, stable post-warmup camera,
document status and Undo/Redo availability. Additional GUI checks exercise an
actual offered input face, pending face/thickness Cancel, changed thickness OK,
Undo/Redo and native save/load. Kernel calculation speed is not the measured gain.

All six selected contracts passed in 79.05 seconds: Shell commands, treatment
GUI, refresh scope, selection filters, surface profiles and five-language
translation catalogs. The local Windows build succeeded. No product UI text
changed; diagnostics and documentation are English. The development BAT remains
unchanged. Linux verification is separate.

Evidence: [before/after and regression logs](20261001-shell-properties-opening.txt).
