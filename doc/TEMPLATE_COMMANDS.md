# Drawing template editor through CLI

`template.new/get/open/save/sketch.edit` operate on the same `.frmz` drawing formats
and `.tblz` title blocks as GUI. These are existing configuration-editor formats;
loaded frames, title blocks, and images remain embedded in native drawings.
No new format or required model sidecar is introduced.

```json
{"command":"template.new","arguments":{"kind":"title_block","name":"My title block"}}
{"command":"template.sketch.edit","arguments":{"operations":[{"command":"sketch.segment.create","arguments":{"first":[0,0],"second":[-20,0]}},{"command":"sketch.text.create","arguments":{"value":"&name","position":[-5,3],"height_mm":2.5}}]}}
{"command":"template.save"}
{"command":"template.save","arguments":{"path":"Copy.tblz","copy":true}}
{"command":"template.open","arguments":{"path":"My title block.tblz"}}
{"command":"template.get"}
```

Kinds are `drawing_format` and `title_block`. Creation neither calculates nor
immediately writes files. Opening an already open file preserves unsaved changes.
Saving without `path` uses the current path. An existing new destination requires
`overwrite: true`; another open document cannot be overwritten. `copy` preserves
the source path and original document's saved state. Save results contain `paths`
with the written file. Paths to the same physical file do not create separate documents.

A batch contains 1–1000 Sketch editing commands without `sketch` or `document`
arguments. It uses a working copy and one commit, or makes no change on failure.
`operation_index` identifies the failed command, starting at zero. External model
references are forbidden in templates. Text uses the editor's native font and
coordinates; the default text is drawing text. `body_calculated` is always false.
`changed: false` adds no history. Ordinary `undo`, `redo`, `close`, and `activate`
also work in an idle template editor.

GUI permits these commands only while the template editor is idle. Active drawing,
dragging, and dialogs reject them. This does not enable model operations belonging
to other workspaces. Toolbar and console share active Sketch-command detection.

Separate image and BOM-region properties are covered in
[TEMPLATE_OBJECT_COMMANDS.md](TEMPLATE_OBJECT_COMMANDS.md). Existing frame and title
block metadata survives opening and saving.

Verification: targeted model/GUI tests **2/2 in 75.55 s** and full regression
**140/140 in 563.84 s**, including actual CLI process, translations, and template
editor. Both applications and all test programs built. The catalog at this stage
contains 251 commands. Log: `build/template-lifecycle-full-tests.log`.
Separate images and BOM regions are next; this stage does not complete CLI coverage.
Push remains deferred under the user's latest instruction at this stage.
