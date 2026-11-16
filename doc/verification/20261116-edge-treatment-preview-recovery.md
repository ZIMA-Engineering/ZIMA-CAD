# Fillet/Chamfer preview isolation and recovery

## Scope

Windows Release, Qt 6.11.0 and OCCT 8.0.1. The baseline is the uncommitted
working tree over `ae668ed58`, including the existing endpoint/node changes.
The user supplied `03.prtz`; all experiments use an isolated copy taken before
the later Chamfer/Fillet shown in the screenshots. The original file is not
overwritten. No native schema, side identity, placement contract or dependency
version changes are involved.

## Reproduced cause

Picking the three upper U-shaped pocket edges in Fillet Properties retained
all three routes but disabled OK with a same-domain cleanup error. A clean
process could calculate these edges individually and together. In the failing
GUI sequence, a terminal alternative first succeeded; the next operation's
input was already invalid. Its volume changed from 1018954.872002629 to
1026807.531512369 cubic millimetres without a document transaction.

Phase checks narrowed the mutation to result-packet persistence, after the
Fillet builder returned with its input still valid. `RemoveUnusedPCurves`
removed p-curves from edges shared with earlier live history boundaries.
A p-curve absent from the new result could still be essential to the input.
Subsequent operations reused that damaged native input.

Serialization now cleans private copied topology and maps original bindings
to that copy. Geometry and triangulations remain shared; live inputs retain
their p-curves. The native archive and reference binding contracts are unchanged.

Secondary Boolean helpers also used two-shape constructors, which execute a
Boolean before `SetNonDestructive(true)` and the subsequent explicit `Build`.
They now set arguments and options before their single build.

## Preview work

Joined nodes expose spike/rounded-node choices, not terminal-face cuts.
Their labels previously enumerated and calculated the unused Cartesian
product of terminal-face alternatives. Those unused trials are removed;
free-end cut choices still calculate when their menu is explicitly opened.
Existing exact node-choice checks remain in place.

Synchronous endpoint initialization no longer re-enters the complete preview
refresh. Failed exact previews are cached with their full pending definition
and input generation, just like successful ones; edits invalidate that result.
Node-option reuse additionally checks the open Part's runtime identity, so
closing/reopening the same native document cannot reuse stale choices.

Properties also retains an existing calculation method. Explicit OCCT is no
longer replaced by the unfinished working-tree change that hid its control.
The current stored Automatic policy is preserved when editing its parameters;
variable Fillets display its actual OCCT path instead of converting to an
unsupported strict ZIMA variable Fillet. Changing the method or treatment type
remains explicit. New ordinary constant Fillets default to strict ZIMA.

## Verification

The baseline GUI sequence issued 69 primary Fillet Boolean batches, of which
60 already received invalid input. These counts exclude helper Boolean builds.
The diagnostic command test can independently enumerate every terminal
combination, including combinations no longer needed by the GUI, then compare
the next valid preview. This distinguishes input safety from merely avoiding
the triggering menu probes.

`ZIMA_VERIFY_PICK_RECOVERY` extends the actual mouse-picking GUI fixture with
invalid/valid parameter edits, failure/Cancel, a subsequent command of each
kind, native save/reopen, Undo/Redo, and unchanged OK. `ZIMA_VERIFY_PICK_CHAMFER`
starts that sequence with Chamfer. Fixtures and logs are disposable build data
under `build/test-models/pocket-diagnosis-20261116`.

### Results

| Check | Result |
| --- | --- |
| Three actual mouse-picked upper pocket edges | Three routes retained, exact preview valid, OK enabled |
| Fillet failure, corrected radius, another failure/Cancel, then new Fillet and Chamfer | Passed, including native save/reopen, unchanged OK and Undo/Redo references |
| Same recovery sequence starting with Chamfer | Passed |
| Independent enumeration of terminal alternatives on the user model | Four Fillet and one Chamfer alternatives accepted; the subsequent identical preview retained exactly equal vertices/triangles and unchanged revision/generation |
| Native planar Fillet/Chamfer, command, direct batch and history recovery CTests | 5/5 passed |
| GUI fragments, method selection, full editing, creation, symmetric endpoints and ordinary selection CTests | 6/6 passed |
| Translation coverage/catalog and live language contract | Passed for cs/en/de/fr/ru |
| Shoulder spike versus rounded-arc nodes, both route orders | Both valid, distinct result volumes; input validity and volume retained |

For the same cold three-edge GUI sequence, primary Fillet Boolean batches fell
from 69 to 16 (about 77% fewer); none of the final 16 received invalid input.
The observed full verification-process time changed from 12.98 s to 8.86 s.
Those wall times include startup, mouse-candidate scanning, screenshots and
diagnostic tracing, so they are not isolated interaction latency or a universal
speedup claim. The operation count directly demonstrates the removed work.
Unneeded preliminary helper builds are eliminated in addition to that count.

The native regression checks include rejected explicit ZIMA calculations and
the following valid preview, as well as genuine spike/arc geometry and reversed
route order. The GUI method test covers saved explicit ZIMA, explicit OCCT and
Automatic definitions, constant/variable mode and explicit method changes.

The verification uses Windows Release executables in
`build/cpp-windows-release`; the normal root `zima-cad.bat` still starts that
build. This is a local development binary, not a versioned portable release.

No user-visible strings were added or changed by this fix. Linux execution
and performance have not been verified on this Windows host.
