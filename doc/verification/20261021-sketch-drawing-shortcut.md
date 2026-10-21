# Drawing shortcut visibility during Sketch editing

The document-kind shortcut above the Tree now hides whenever a Sketch is being
edited. Finishing the Sketch restores the ordinary Part/Assembly Drawing
shortcut. Its navigation handler also rejects invocation during Sketch editing.
The existing restrictions for library templates and symbols remain in place.

The cause was independent visibility handling: the neighbouring document tools
already checked the active Sketch, while the document-kind button checked only
the displayed document. The fix reuses the existing active-Sketch state and scene
refresh. It introduces no model calculation, persistence change or UI text.

## Finite Windows GUI matrix

Run the native GUI with `--working-directory <isolated-directory>
--verify-startup` and `ZIMA_VERIFY_SKETCH_DOCUMENT_BUTTON_ONLY=1`.
Use isolated `config.ini` settings for each language.

| Dimension | Cases |
| --- | --- |
| Document context | Ordinary Part with an active Body; Assembly Sketch |
| Initial appearance | Light, Dark, Light; palette checked after window creation |
| Editing lifecycle | Two successive openings through the actual Sketch Properties button |
| Inside Sketcher | Drawing shortcut hidden, including a live theme change |
| Exit | Finish Sketch returns to Properties and restores the shortcut |
| Properties dismissal | Cancel on the first cycle; OK on the second |
| UI language | Czech, English, German, French, Russian |

The verifier captures both editing and outside-Sketch states. Results and
captures are stored under `build/sketch-document-button/` in the local workspace.
The catalog/source coverage contract is `zima_cpp_translations_contract`.

On 2026-10-21 the local Windows production GUI passed all 30 combinations
(60 successive editing cycles), and the localization coverage contract passed.
The normal repository-root `zima-cad.bat` launches this updated local executable.
This correction is not part of the immutable Windows 2026102004 archive.

Geometry constraints, dimensions, point manipulation, native supports and
external supports are unchanged: this correction only controls document
navigation visibility. Those solver combinations are outside this UI matrix.
There is no new native data or factory/template conversion. Linux GUI execution
is not verified on this Windows host.
