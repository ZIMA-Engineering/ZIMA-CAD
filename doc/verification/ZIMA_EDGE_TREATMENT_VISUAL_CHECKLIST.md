# ZIMA Fillet and Chamfer visual acceptance checklist

This is the stable, user-directed checklist for manual ZIMA-CAD acceptance.
Case identifiers do not change when a feature is renamed or when another case
is inserted. The source models must be opened only through disposable copies.

Possible results are `NOT RUN`, `ACCEPTED` and `REJECTED`. The catalog must not
be described as visually complete while any applicable case is `NOT RUN` or
`REJECTED`. Normal release acceptance requires all applicable rows to pass. An
explicit user-authorized candidate may be published with open rows only when
the release record lists those rows and does not claim complete visual
acceptance.

## Automated candidate precheck

On 2026-11-14 the current Windows candidate cleanly regenerated disposable
copies of `RADIUS.prtz`, `READIUS2.prtz` and `RADIUS-03.prtz`. It then opened
Properties at the rollback boundary for RADIUS Fillets 04, 05, 09, 11, 12, 13
and 14, RADIUS Chamfer 01, READIUS2 Fillet 03 and RADIUS-03 Fillet 01. Every
persisted route resolved, the published transient strokes exactly matched the
analytical preview, Cancel preserved the document revision and developer
inspection found continuous rails and terminal sections in all ten captures.

The GUI regression now independently requires each analytical preview rail to
end on its final terminal section; merely publishing the same strokes returned
by the preview generator is no longer sufficient. Those unlabelled terminal
contacts are verified separately from the numbered view nodes. The labels
identify every distinct source node along a connected selected path: `1.1`,
`1.2`, `1.3`, and so on. The next disconnected path starts at `2.1`. A loop's
closing node is the same node as `1.1` and is not duplicated. Curve display
samples and generated treatment contacts remain unlabelled. Both ordinary and
symmetric creation pass these checks, as do read-only Properties runs for the
retained R4 four-edge Fillet
09, current RADIUS Fillet 05 and Chamfer 01, and current READIUS2 Fillets 01 and
03. A synthetic five-edge cylindrical route also passes R4 through R18 around
an adjacent R10 treatment without escaping the Body envelope or continuing
past either real route end.

The current user model has evolved from the historical rows below. In its
2026-11-14 state, Chamfer 01 is A5 on two independent one-edge source routes
and Fillet 15 is R4 on one persisted source edge which resolves to a connected
five-member U route. Disposable copies regenerated and previewed both cases.
That earlier precheck displayed endpoint-only labels and therefore does not
satisfy the current all-source-node contract. No treatment-contact labels were
shown. This remains an automated precheck and does not mark any manual row
below as accepted.

This precheck does not change any manual result below. Some current user files
contain features or parameter values that differ from the earlier stable case
catalog, so manual acceptance must still use the exact row definition and
record the candidate build and tested parameters.

## 2026-11-16 automated follow-up

The current candidate adds independent persisted `Cut` / `No cut` decisions for
both ends of every open route. `No cut` is the default and prevents a treatment
tool from continuing through material to a remote surface. Purple source-node
labels expose the two choices by double-click for open ends; closed loops do not
offer an endpoint menu. The GUI contracts verify creation, Properties editing,
Cancel, OK, Undo/Redo, native save/reopen and preview/result endpoint agreement.

The kernel matrix now also covers opposite trim directions, asymmetric local
boundaries, bounded Fillet-to-Fillet contact, circular A x B Chamfer, Chamfer
mode parity and a Shell following three Fillets. All 1,058 repository-owned
Part-based assets were converted by the current implementation to native format
53, calculated, saved and reopened in a separate staging tree before their exact
verified outputs replaced the repository copies. This remains automated evidence
and does not change the manual row results below.

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
