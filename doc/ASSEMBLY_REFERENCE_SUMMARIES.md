# Reference summaries during Assembly history and regeneration

A Part stores the actual external reference; an Assembly stores its derived
dependency between immediate branches. Undoing an independent Assembly change
must not restore an old summary inconsistent with the current Parts.

## History transactions

`step_assembly_document_history` moves history only in a private candidate.
Shared preparation checks the candidate hierarchy and affected open contexts,
reconciles summaries with source Parts, and prepares the required owner states.
Publication requires no further allocation. On failure, the live document,
revision, history, and other documents remain unchanged. Candidate history is
moved into the result once; source Parts are neither recalculated nor copied
into the Assembly.

The same traversal of persisted occurrence paths can use a candidate source
Assembly during preparation. This allows restoring a nested branch absent from
the currently displayed tree. It reads identities without calculating placement,
mates, or bodies.

If a source is unavailable, retain current and historical summary edges whose
endpoints both exist in the candidate. A resulting cycle rejects the entire
step. The step can be retried once the source becomes available. Known
dependencies retain their persisted identity during restoration.

When a reference changes direction, remove all demonstrably stale edges of the
affected owner before adding new edges. An old opposite dependency must not
cause a false cycle rejection.

## Explicit regeneration

`regenerate_assembly` first reconciles summaries in the requested hierarchy,
including closed native subassemblies. Open documents take precedence over
files. Closed source Parts are read privately without opening tabs. Traversal
keeps small reference sets keyed by Part identity rather than loaded calculated
bodies. The native loader currently still reads the entire Part.

A changed closed owner is published as an open, unsaved Assembly so its repaired
data can be saved into its `.asmz`. Summary changes create no separate Undo entry.
If nothing changed, no additional document opens. Source Parts and the repaired
Assembly must be saved normally.

Summary preparation and publication are atomic. Subsequent model regeneration
still runs per document; this stage introduces no global calculation transaction.
Switching tabs triggers no calculation. Native formats, templates, and mate
solving are unchanged; no required external caches or sidecars are introduced.

## Verification

The original regression failed: Assembly Undo removed a summary for a reference
still present in the Part (`build/assembly-reference-summary-baseline-tests.log`).
After the change, both model tests passed: **2/2 in 0.69 s**
(`build/assembly-reference-summary-identity-tests.log`). Coverage includes:

- Creating and detaching a reference between independent Assembly history steps.
- Latest unsaved Part data and preservation of its calculated allocation.
- Editing a Part with its context closed, reopening Top, and repairing the native owner.
- Creating the opposite reference after detaching the original with its context closed.
- Unverifiable sources, atomic cycle rejection, and a successful later retry.
- Undo restoring a nested owner absent from the currently displayed tree.

Process and GUI regressions include independent Assembly Undo. The GUI also tests
a source-file read failure: the rejected step must be reported and history retained
for retry after repairing the file. Both applications and all test programs built
successfully (`build/assembly-reference-summary-integration-build.log`). The CLI
process and both model tests passed **3/3 in 21.22 s**; the separate GUI regression
passed **1/1 in 18.57 s**. The **full suite then passed 119/119 in 528.05 s**
(`build/assembly-reference-summary-full-tests.log`), including that GUI regression,
the console, application startup, drawings, translations, Sketcher, and native formats.

The catalog still has 209 commands. This stage adds their shared data transaction;
it does not complete the remaining CLI areas. Shared root-construction removal
is next, as described in [CAD_COMMAND_COVERAGE.md](CAD_COMMAND_COVERAGE.md).
