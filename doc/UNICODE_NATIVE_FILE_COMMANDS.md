# Unicode paths in native file commands

## Repaired GUI/CLI difference

On Windows, the narrow `std::filesystem::path` constructor accepts system-encoded
strings, while `QString::toStdString()` supplies UTF-8. Passing one directly to the
other can corrupt Czech file/directory names. Likewise, `path.string()` must not
be treated as UTF-8.

The shared command layer already uses `std::filesystem::u8path` and
`document::path_to_utf8`. The same conversion now applies to these GUI adapters:

- Initial window working directory, supplied directly or through config.
- New Document, Open, and opening-progress titles.
- Save As for Part, Assembly, and Drawing, including automatic copies of owned drawings.
- Working-directory selection.
- Drawing Save As JPG/DXF.
- Switching between model and drawing when opening a source file is necessary.

Ordinary Save and tab titles were fixed during [NATIVE_FILE_RENAME.md](NATIVE_FILE_RENAME.md).
This changes neither model calculation, shared placement, nor native schemas.
Start templates are unchanged. No new command is added: the catalog remains at
**288 commands**, with **154 tests** registered at this stage.

## Verification with actual files

`verify_save_copy_ui` now creates a temporary directory containing accented characters
and checks GUI together with the console:

1. Initial working directory exactly matches the directory supplied to the window.
2. A Part with an actual calculated body and its drawing are saved under Czech names.
   GUI Save As creates both copies with new IDs and redirects the drawing to the
   copied Part. Original document, tab, and activation remain unchanged.
3. GUI drawing export creates readable JPG and complete DXF. `export.image` reads
   the same Czech-named source and creates a readable image.
4. GUI-selected working directory matches `context`.
5. GUI New, Save, and Save As run for all three native types. Saved documents are
   checked for actual IDs; CLI then opens every created copy.
6. Test documents close with explicit discard and no manual dialog.

File selection is handled repeatedly and unexpected error messages are checked.

Windows Release built both applications and all targets
(`build/unicode-native-gui-final-build.log`). The focused scenario passed
**1/1 in 4.21 s** with `ZIMA_VERIFY_SAVE_COPY_ONLY=1`
(`build/unicode-native-gui-focused-final-tests.log`). Subsequent regression passed
**5/5 in 258.44 s**: native documents, document operations, actual CLI, GUI console,
and full workspace-window test (`build/unicode-native-gui-regression-tests.log`).
Both GUI runs required no manual confirmations.

The first new-fixture run omitted required `sheet` for `export.image`. It now supplies
the actual saved sheet ID; the export interface was unchanged. Successful results
above were recorded after this correction.

This verifies the listed native workflows, not every remaining path conversion in
the application or external launchers.
