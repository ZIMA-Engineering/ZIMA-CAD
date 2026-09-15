# Opening components in GUI and CLI

`opening.components` and `hole.components` list currently displayed components as
the tree does. Each row contains stable `role` and `removable`; identity consists
of the owning `container` and role. Queries perform no calculation.

```json
{"command":"opening.components","arguments":{"container":"OPENING-ID"}}
{"command":"opening.component.remove","arguments":{"container":"OPENING-ID","role":"chamfer"}}
{"command":"hole.component.remove","arguments":{"container":"HOLE-ID","role":"thread"}}
```

For the current Opening (`opening`, native Thread), `thread`, `chamfer`, and `tip`
can be removed. Required `bore` cannot be removed separately. Native Hole permits
only separate `thread` removal, matching GUI. Its other components, construction
planes, and owned Sketches are edited through full Hole Properties. Both variants
retain their existing model semantics.

GUI and CLI share `remove_opening_component` and the data mutation
`disable_opening_component`. Opening commits through existing `commit_opening`;
removing the threaded surface preserves the bore diameter. Native Hole threads
are parameter-derived wires, so removal only commits the document with its existing
calculated body.

The result returns opening properties, `component`, `changed`, and `body_calculated`.
One Undo restores the complete original state. Removing an already disabled optional
component returns `changed: false` without calculation or history. Unknown or required
roles are rejected. Derived or inactive bodies and pending GUI edits cannot be modified.
All owned profile IDs remain intact. The native format is unchanged.

Verification (2026-09-14): model suite **4/4 in 2.64 s** and final integration
**12/12 in 128.16 s** passed. Coverage includes the original geometry test with
analytic volumes, original end references, actual CLI process, GUI console, and
both context menus. Checks include no-ops, inactive-body rejection, profile
preservation, and complete restoration by one Undo even after creating another
history branch. Both applications and all test programs built. Logs:
`build/opening-components-model-tests.log`,
`build/opening-components-integration-build.log`,
`build/opening-components-integration-tests.log`.
