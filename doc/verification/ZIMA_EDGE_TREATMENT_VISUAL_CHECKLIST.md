# ZIMA Fillet and Chamfer visual acceptance checklist

This is the stable, user-directed checklist for manual ZIMA-CAD acceptance.
Case identifiers do not change when a feature is renamed or when another case
is inserted. The source models must be opened only through disposable copies.

Possible results are `NOT RUN`, `ACCEPTED` and `REJECTED`. A Fillet/Chamfer
change must not be committed, pushed or released while any applicable case is
`NOT RUN` or `REJECTED`.

## `RADIUS.prtz`

| Case | Feature | Manual check | Current result |
| --- | --- | --- | --- |
| RADIUS-01 | Fillet 03, ZIMA R10 | The Fillet ends at the true terminal intersection, does not cross the red terminal face and leaves no chamfer-like remnant. | NOT RUN |
| RADIUS-02 | Fillet 05, ZIMA R6 | Both one-edge branches form a clean corner without teeth, doubled caps or protruding radius fragments. | NOT RUN |
| RADIUS-03 | Fillet 04 ZIMA reproduction | The complete analytical preview and result remain correct while the radius is swept through the recorded feasible interval. The saved OCCT feature is not a ZIMA proof. | NOT RUN |
| RADIUS-04 | Fillet 09, ZIMA R4 | Preserve the previously working compact four-edge treatment. No large tent/ear or untrimmed auxiliary geometry is allowed. | PARTIAL — 2026-11-13: the persisted feature still showed the defect, but deleting and recreating it with the local candidate produced the correct result. Persisted-reference recovery remains unresolved. |
| RADIUS-05 | Fillet 10, ZIMA R4 | The seven-edge mixed-owner route keeps all members and produces cleanly trimmed terminals. | NOT RUN |
| RADIUS-06 | Fillet 11 reproduction | Sweep the radius below, near and above the adjacent Fillet radius. The result remains continuous until an actual geometric limit. | NOT RUN |
| RADIUS-07 | Chamfer 01, ZIMA A=2 | The five-edge Chamfer calculates, follows its own azure route and uses the same correct terminal principles as Fillet. | NOT RUN |
| RADIUS-08 | Invalid dependent Fillet repair | Make the first of the last dependent Fillets invalid, then edit the next one. ZIMA-CAD must not crash and references and radius remain editable. | NOT RUN |

## `READIUS2.prtz`

| Case | Feature | Manual check | Current result |
| --- | --- | --- | --- |
| RADIUS-09 | Fillet 01, ZIMA R14 | Preserve the calculated two-branch geometry and display the complete authored azure Fillet preview for both branches and terminal sections. Both longitudinal preview rails must end exactly on the circular terminal arc; no endpoint may continue beyond the circle. | FAIL — 2026-11-13: both straight azure rails extend beyond their circular terminal arcs. See `evidence/20261113-radius-09-overextended-preview.png`. |
| RADIUS-10 | Fillet 02, ZIMA R11 | Both one-edge branches and their terminal trimming remain correct. | NOT RUN |
| RADIUS-11 | Fillet 03, ZIMA R18 | The analytical azure preview uses exactly the same route and terminal solution as the calculated result. | NOT RUN |
| RADIUS-12 | Fillet 04, ZIMA R4 | Preserve the nine-edge multi-owner route. Check radii below, equal to and above the neighboring treatment radius wherever geometry remains feasible. | NOT RUN |
| RADIUS-13 | Fillet 05, ZIMA R4 | The current nine-edge route must calculate and the preview must match the final route; the present `OCCT did not create a Fillet contour` failure is not accepted. | NOT RUN |
| RADIUS-14 | Additional failing edge A | Capture the exact selected owner/key, radius, screenshot and expected terminal solution when the user presents the edge. | NOT RUN |
| RADIUS-15 | Additional failing edge B | Capture the exact selected owner/key, radius, screenshot and expected terminal solution when the user presents the edge. | NOT RUN |

## Acceptance record

For each run record:

- candidate commit and build ID;
- date and platform;
- case identifier;
- tested feature parameters;
- `ACCEPTED` or `REJECTED`;
- screenshot or concise rejection reason;
- whether save/reopen, Undo/Redo, preview and generated-reference selection were
  applicable and verified.

The detailed behavior contract and automated-test requirements are in
[`ZIMA_EDGE_TREATMENT_REGRESSION_MATRIX.md`](ZIMA_EDGE_TREATMENT_REGRESSION_MATRIX.md).
