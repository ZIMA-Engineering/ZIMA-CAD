# Sketch offset — design note

Status: idea for later design. Do not implement without resolving the open
geometry and interaction contracts below.

## Purpose

The main value is not offsetting a single line, which is easy to construct
manually, but offsetting geometry that is difficult to reproduce by hand:
B-splines, ellipses, arcs, closed curves and connected curve chains. Both
native Sketch geometry and read-only external geometry should be usable as
sources.

## Inputs → means → outputs

- Inputs: one curve or an ordered connected chain, signed distance/side,
  join policy and approximation tolerance.
- Means: a deterministic ZIMA 2D offset calculation in the resolved Sketch
  plane. Ordinary UI, hover, picking and reopening must not invoke OCCT.
- Output: native geometry owned by the active Sketch, with a persisted,
  one-way dependency on stable source IDs. External sources remain read-only
  and never transfer ownership.

## Preferred data model

Prefer a semantic, parametric `OffsetCurve`/`OffsetChain` object over an
immediately baked collection of unrelated control points. Persist at least:

- stable source curve/chain IDs and their order;
- distance and selected side;
- open/closed state;
- join policy (sharp/miter, round or bevel);
- approximation tolerance;
- stable identity of every retained result branch.

The calculated result may be represented by native arcs/segments where exact
and by an approximating B-spline where necessary. A mathematical offset of a
B-spline is generally not another B-spline of the same degree and control
polygon, so copying or simply displacing its control points is not valid.

## Required interaction

- Hover previews the offset without committing it.
- For an open curve, cursor position selects the side; do not rely only on a
  hidden sign convention.
- For a consistently oriented closed profile, present inside/outside clearly.
- If calculation produces multiple valid branches, show all branches and
  require an explicit retained-branch selection. Never silently choose by
  enumeration order.
- OK commits the offset object; Cancel leaves the Sketch unchanged.
- Editing consumes persisted ZIMA data and uses the same interaction class as
  creation.

## Geometry hazards that must be designed first

- A cusp/singularity can occur where offset distance times curvature reaches
  or exceeds approximately one.
- High curvature can create local loops and self-intersections.
- A closed profile can split into several disconnected branches or vanish
  locally when offset inward.
- Connected curves need a defined join policy and miter limit.
- Tangency and curvature continuity at joins must be evaluated rather than
  assumed from visual proximity.
- Periodic/closed splines, reversed curves, very short spans and degenerate
  control polygons need deterministic handling.
- Approximation error must be measured against the mathematical offset, not
  merely against sampled input points.
- Result identity must not depend on traversal/enumeration order.

## Dependency rules

- A native source creates an internal one-way dependency.
- An external source creates a read-only external-reference dependency owned
  by the active Sketch.
- Reject direct and indirect dependency cycles.
- Regeneration uses currently open authoritative documents according to the
  repository's explicit dependency-regeneration contract.

## Suggested staged scope

1. One open B-spline with one unambiguous result branch.
2. One closed curve, including explicit inside/outside selection.
3. Connected mixed chains with join policies.
4. Multiple branches, self-intersection handling and stable branch identity.
5. External-source regeneration and editing coverage.

Each stage needs deterministic geometry, tolerance and persistence tests plus
interactive tests for preview, side selection, branch selection, OK/Cancel,
reopening and regeneration. During exploration unusual branches may be useful;
before acceptance every retained branch must pass rigorous geometry checks.
