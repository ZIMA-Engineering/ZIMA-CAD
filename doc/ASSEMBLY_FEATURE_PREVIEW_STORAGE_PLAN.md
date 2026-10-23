# Stored Assembly Feature preview wire

Status: approved by the user on 2026-10-23; implementation and verification
are in progress.

## Demonstrated gap

An Assembly cut currently stores its definition, selected occurrences and exact
pre-cut component snapshots. `OcctKernel::subtract_bodies` deliberately preserves
only component-owned reference geometry. Consequently, the Assembly-owned
cutter's original wire is absent after calculation/reopening. It cannot be read
from a component's original-reference packet, and opening Properties must not
calculate it implicitly.

## Proposed native data

Add a dedicated presentation-wire field to each stored Assembly cut in `.asmz`.
Explicit successful cut calculation fills it from that cut's actual original
operand, in the owning Assembly frame. Keep semantic source identities and omit
parameter seams. Do not duplicate the native cutter BRep or add an external cache.

Ordinary hover/selection, double-click inspection and unchanged Properties consume
this one stored wire. It is presentation data, not new selectable body topology.
Numeric/reference edits retain the exact private preview path. An unavailable
stored wire is reported without calculation. This field does not change Part,
Drawing, placement, subtraction or geometry-side equations.

## Scope and verification

Audit tracked Assembly assets and their dependent Drawing use. Calculate, save
and cold-reopen every affected repository-owned Assembly; compare geometry,
authored definitions, references, sides and physical values. Regenerate all
affected Assembly start templates and verify actual GUI new-document creation,
initial editable component context and command availability. The already approved
curve/plane change retains its separate six-template and native-asset gates.

Verify multi-target/corrected Extrusion and Revolution, Through-all combinations,
placed and repeated occurrences, hidden wire, unchanged opening/OK without kernel
calls, changed OK, Cancel, Undo/Redo and source motion followed by explicit
regeneration. Count scene publications and native source preparation; measure
file-size overhead and complete all five UI languages. Report Linux verification
separately. Do not bulk rewrite user projects or custom libraries.
