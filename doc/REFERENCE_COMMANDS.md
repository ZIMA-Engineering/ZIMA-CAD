# Queries for original references

`reference.list` and `reference.get` read persisted original-reference packets from
Part or Assembly. They invoke no OCCT, activate no document, change no selection,
and load no newer dependency contents. Result-body topology is not added to original
references. Queries are also allowed during editing.

## Interface

| Command | Arguments |
| --- | --- |
| `reference.list` | `[owner kind instance_path document offset limit]` |
| `reference.get` | `kind owner key [instance_path document limit]` |

`kind` is `face`, `edge`, `point`, or `axis`. Identity consists of kind, stable
`owner`, semantic `key`, and exact opaque `instance_path`. Pass returned paths
unchanged; they are neither component names nor file paths. Repeated Part occurrences
have different paths. Omitting the path means the empty document path, not automatic
occurrence selection.

JSON is convenient for optional filters:

```json
{"command":"reference.list","arguments":{"kind":"face","owner":"<feature-ID>","limit":50}}
{"command":"reference.get","arguments":{"kind":"face","owner":"<feature-ID>","key":"<key-from-list>","instance_path":"<path-from-list>"}}
```

Lists return `items`, `total`, `offset`, `more`, `next_offset`, document ID, and
revision. Default limit is 500, maximum 5000; index is 0–100,000,000. Pagination is
stable for the same document state. A reference repeated on several triangles
appears only once.

## Geometry and accuracy

Details use the queried document's coordinates, lengths in mm, and areas in mm².
Only data actually present in the persisted packet is returned:

- Face: triangle samples, total triangle count, measured area, and exact analytic
  plane/cylinder/cone when stored. Cone `semi_angle_radians` is in radians.
- Edge: fragments with stored points, measured length, seam and infinite-line flags.
  Exact stored splines include degree, poles, weights, and knots.
- Point: position. Axis: point and direction.

`null` analytic surface, length, or spline means the packet lacks that data. Edge
samples are not presented as exact curves, triangles are not reverse-fitted into
analytic surfaces, and splines undergo no fitting or new approximation.

Detail `limit` defaults to 256 and accepts 1–10000. It limits face triangles and
total edge samples/fragments. Full spline data is returned only if the combined
pole, weight, and knot count fits a separate limit of the same size. Truncation is
explicit in `samples_truncated`, `segments_truncated`, and optional
`spline_omitted_by_limit`. Response limits change neither model nor precision.

Listing traverses original geometry directly from shared snapshots without copying
the displayed Assembly. Helper Origins and constructions are derived from persisted
model data by the same functions as GUI. A single-reference query transforms only
returned data. Analytic surfaces in nested snapshots use the source frame, while
samples and spline poles already include internal placement; each representation
uses its corresponding persisted occurrence chain.

A query does not guarantee a reference is valid for every command. The receiving
operation checks ownership, history order, active body, and dependencies. Preserved
geometry of hidden/suppressed immediate components remains readable;
`source_occurrence_visible` and `source_occurrence_suppressed` describe that immediate
component, not every internal feature.

## Verification

Model tests cover original box faces, pagination, response limits, original points,
exact splines, exclusion of result topology, repeated/nested occurrences with rotation,
unchanged revisions/snapshots, and Assembly reads after an open source Part changes.
GUI checks confirmed-selection preservation; actual CLI checks references from saved
and reopened `.prtz` files.

Targeted Windows Release regression passed **4/4 in 10.14 s**,
`build/reference-integration-tests.log`. GUI and CLI built from the same source
(`build/reference-integration-build.log`). The preceding full history regression
passed **61/61**; this stage adds a reference-reading test and verifies all affected
command adapters. It performs and changes no geometric calculation.
