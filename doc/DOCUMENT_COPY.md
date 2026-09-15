# Save As: copying a document and its drawings

**Save As** stores the current working state under a new name. The original
remains open and active, with the same ID, path, and unsaved-change state.
The operation neither renames it nor marks it saved.

The new Part or Assembly receives a new document ID. Feature, body, source-curve,
and topology identities remain within its own namespace. Self-document and
self-Origin references are redirected to the new identity. Assembly Parts and
subassemblies still reference their original sources; the entire dependency
graph is not automatically copied.

Linked drawings are copied with the model. Current open documents take precedence
over disk files. Closed drawings are searched for in the source model directory
and current working directory. The main sibling drawing uses the new model's
basename and `.drwz`; additional drawings use `_2`, `_3`, etc. Drawing copies
receive new IDs, and their model references, paths, and views point to the model
copy. Standalone Drawing Save As creates only a drawing copy with the same source model.

When copying to another directory, relative paths are anchored to the source
directory. Stored geometry, including B-Rep and intermediate body results, is
reused without OCCT. Stored-result fingerprints adapt to identity/path changes
without recalculating geometry.

All destination names are checked before writing. Existing or open destination
files are not overwritten. Copies are first written and reloaded in a temporary
directory beside the destination; only verified files are published. Publication
uses atomic hard-link creation without overwriting existing files; temporary
links are then removed. On error, only new files created by this operation are
removed. Originals remain unchanged.

Verification covers unsaved model/drawing state, closed drawings, concurrent
opening of originals and copies, destination drawing collisions, Assembly mates,
standalone Drawing copies, and copying frozen STEP data to another directory.
The GUI test invokes the actual Save As command and checks that the original tab stays active.
