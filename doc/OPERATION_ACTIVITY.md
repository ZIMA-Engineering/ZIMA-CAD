# Shared operation activity

User agreement: 2026-10-07. The primary indicator appears at the center of the
visible model or Drawing View, above an overlapping Properties window. A shared
Properties confirmation also shows a compact indicator beside OK. Both use the
same restrained cyan animation. The label describes activity rather than an
estimated time or invented completion percentage.

## Lifecycle

`ui::OperationActivity` presents explicit work only. It does not calculate,
regenerate or commit documents. Its scoped lifecycle starts on shared Properties
confirmation, explicit regeneration and console commands. Existing file-operation
phases reuse the same controller. Nested phases update the message; completing a
phase restores its parent. Exiting an owning operation retires unfinished child
phases too. Success, rejected validation and exceptions retire scoped activity.

Appearance is delayed by 250 ms so fast actions, including unchanged OK, do not
flash. Resize and workspace changes keep the panel centered on the visible View.
The existing status progress bar retains real measured progress where available.
The central activity panel itself never claims a completion percentage.

## Responsive calculations

The GUI installs a scoped calculation runner while an explicit operation is
active. The shared Workspace calculation entry points can then dispatch Part
candidate calculations and resolved Assembly cuts to a worker and wait while Qt
paints the activity indicator. They still calculate the same candidate, references
and geometry. Their caller commits the result on its original owner thread.
Native and CLI callers use synchronous execution unless they explicitly install
a runner. Worker execution temporarily clears the owner-thread runner, preventing
recursive dispatch even when a supplied runner executes inline.

Calculations read existing source packets and edit their private pending candidate;
they do not move live Workspace commits, Tree updates or viewer publication to the
worker. The shared background-task wait blocks competing mouse, keyboard, shortcut
and close input while it processes paint and timer events. Input blocking belongs
to actual background execution: validation prompts in an OK handler must remain
interactive. Confirmation also disables its buttons and rejects reentrant submit.

This is a reusable lifecycle, not a claim that every expensive application path
has been audited or moved to a worker. A new integration must distinguish private
calculation from live model mutation and GUI work. Synchronous export preparation
or other uncovered work requires its own ownership audit before worker dispatch.
Do not wrap arbitrary GUI command bodies in a worker to obtain animation.

## Verification status

The focused regression covers delayed appearance, the panel's center and stacking
over Properties, simultaneous OK activity, successive animation frames during a
dispatched calculation job, blocked conflicting input, exception cleanup and nested
phase retirement. It compares worker and synchronous geometry and preserves the
read-only source document. Catalog and shared dialog tests cover all five languages.

The Windows focused activity, catalog and shared dialog regressions passed.
The animation fixture includes a controlled 700 ms worker delay followed by a
real kernel calculation; it does not claim to sample a particular OCCT subcall.
The layout matrix checks 370 language/dialog/size combinations. Actual FORM Sweep,
placed external-reference reprojection, Boundary/Thicken, Assembly refresh and
new-document GUI checks passed. The combined console GUI check passed on Windows
(168.34 s) after its outdated confirmation, unit and library-Sketch assertions
were updated to the established native Part ownership and editing contracts.
Subsequent feature-authoring changes
require rerunning the affected checks before a release.
Linux execution belongs on the Linux host. No native document format changes or
new user settings are required by this presentation.
