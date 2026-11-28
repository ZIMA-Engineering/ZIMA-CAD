# Assembly mechanisms preparation

The first stage adds presentation and application navigation only. An Assembly
Tree contains Sections, Mechanisms and Assembly groups in that order. Origins,
components and authored history remain under Assembly; the Mechanisms group is
empty. Existing native identities, immediate Assembly ownership, history order
and insertion cursors remain authoritative.

Assembly and Mechanisms use the ordinary exclusive application actions and
remember the selected mode independently for each open document. Double-clicking
a group or choosing its Activate context action reuses ordinary top-level or
nested component activation. A single click selects the group without pretending
that it is a component or an exact geometric reference. Activated Parts retain
their ordinary Part applications and the displayed top-level Assembly context.

Mechanisms currently exposes Selection and a disabled preparation message. It
does not create mechanisms, drives, joints or a simulation result. The current
construction geometry remains visible while this empty application is selected.
Switching between these applications only updates commands and Tree feedback;
it must not republish model geometry, solve mates, invoke OCCT, dirty a document
or create an Undo step. No native format extension or required sidecar is added.

The agreed later stage will own named mechanism definitions inside the native
Assembly. Activating one will display its motion state; activating Assembly will
restore the construction state. One state is displayed at a time. Rigid motion
must reuse calculated source geometry and update occurrence transforms according
to kinematic rules, without regenerating material on animation frames. That
future persistent contract and solver are not implemented by this preparation.

The Windows UI gate passed five languages, Light/Dark/Light, group order, mouse activation,
immediate history cursor/reordering availability, nested Assembly and Part
ownership, native saving and zero material-calculation counts in 10.08 seconds.
It also checks the inactive application's text palette after changing theme.
The existing history-cursor, Assembly-presentation and application-tools gates
passed. The new-document test now checks that the Skeleton follows the Origin
inside the Assembly group instead of assuming those rows are direct root children.

An additional `default_origin_nested` regression stops at its older selected-Point
pixel assertion before reaching its later cases. The same failure was reproduced
with the unchanged published 2026112803 executable, commit
`de735241c1553f636c561187c2648cfa4ce86803`; it is not claimed as passing. The new
mode gate independently covers nested Assembly/Part ownership and top-level
display retention. Native Linux execution remains unverified.
