# STEP selection, requested analysis and native save

This Windows follow-up is included in signed build
[2026101402](../releases/2026101402.md#signed-windows-publication), after the
immutable `2026101401` release. The baseline is commit
`8fc129446375fb31f2c2f3672935595790b34c37`, Qt 6.11.0 and OCCT 8.0.0.
Linux execution has not been verified.

## Scope and preserved contracts

Ordinary View selection of a modeling feature selects its owning feature in the
Tree, rather than its internal Sketch. It does not expand that feature. Direct
Tree selection of a Sketch remains available. Tree icons use the same palette
and semantic colors when selected; toolbar checked-state rendering is unchanged.
Double-click inspection still uses the shared inspection path.

The shared container-placement panel and solver are unchanged. The user withdrew
the earlier request to remove placement dimensions. This change does not remove
those dimensions or alter reference sides, offsets or placement persistence.

At the user's explicit request, ordinary Part calculations and Assembly cuts no
longer integrate every face area for a future inspector. Explicit Measurement,
face-property queries and Sheet thickness inference integrate requested missing
areas using exact native faces and their persisted bindings. They do not replay
history, remesh, reconstruct reference identities or create an Undo transaction.
Whole-object inspection completes pending scalar properties on a private copy.
Already available exact values remain usable.

Body packets retain `measurement_sources` inside the native document when an
authored original face is no longer present in the current solid. These sources
contain native geometry and face bindings, without another viewer mesh or nested
history. There are no required sidecar files. Cache reuse carries these sources
with their actual unchanged operand inputs; changing an input invalidates reuse.
Scale, Mirror and Pattern preserve the exact transformed bindings. Linked Shell
sources still receive the complete topology required for later operations.

Warm Subtract verification exposed another reference-cache defect: filtering
the final packet by older owners can retain later intersection descendants
owned by those same sources. The unchanged prefix then contains reference
geometry from the wrong boundary. Subtract histories now reuse the existing
bounded exact-prefix reference cache, also used for feature copies. It retains
at most eight recent reference packets. A missing exact prefix is recalculated;
owner filtering of a later Subtract result is not accepted as that prefix.
This preserves the complete cold-versus-warm packet, rather than ignoring the
changed original edges or loosening geometric comparisons.

Native saving moves independently owned JSON tables instead of copying them
through initializer lists. Large identical strings are indexed by their exact
immutable bytes. ASCII archives need no escaped temporary string just to prove
UTF-8 validity; non-ASCII data retains strict validation. Pool ordering, escaping,
signed zero and full temporary-file reload validation are preserved.

## Representative input

The user's closed `Projects/02.prtz`, saved at `2026-10-14T10:13:05.7941087+02:00`,
contains the imported STEP, empty Sketch, Axis and two Subtract features.
The second subtraction uses Through All. Body translation is `(-1500, 0, -1000)`.
Testing uses an immutable copy; the original is never overwritten.

- File size: 853,508,060 bytes.
- SHA-256: `88285d6c2fd49b5a4299bf9454dd4bc5c705212c83214d3f4dad876638488502`.
- Baseline exact definition digest:
  `37a689159680e41ef22afd8e64753cd429caa7fc90acc1e75c4a27a70db4a439`.
- Baseline complete calculated-packet digest:
  `025b00c0cf5edcb6f92b0985a3b30dfab9bedb1a29f075be99bb5d9ca5fd9765`.

## Measurements and limitations

The original saved model roundtrip decreased from 109,052 to 80,549 ms,
including full reload validation. Saved bytes, complete packet and definition
digests were identical. An intermediate allocation-only sample took 92,893 ms.
Saving newly calculated requested-analysis packets took 89,182 ms and produced
857,069,090 bytes; independent reopening preserved every complete packet and
the authored definition. This separate sample measures different calculated
data and must not be presented as an identical-input save comparison.

The cold last-subtraction probe decreased from 68,088 to 54,627–55,440 ms in the
initial requested-analysis samples. Face-area integration changed from roughly
4.85 seconds per material packet to zero. Native geometry, viewer coordinates,
physical-property state, references and sides matched the saved result after
excluding only the intentionally changed scalar-analysis representation.
A subsequent diagnostic sample took 65,119 ms, so these finite observations do
not establish a universal speedup. Edge publication and archive/serialization
work remain substantial.

Cold probes deliberately remove the last aggregate. That also removes its
branch histories; repeating this setup measures another cold calculation.
Earlier results labeled warm with that setup are discarded. Ordinary edit
verification instead passes the complete calculated aggregate, changes the
last feature and restores its definition. The accepted final Subtract sample
took 56,590 ms cold, 21,463 ms for a changed draft angle and 22,683 ms to restore
it. The complete restored packet matched exactly, including native archives,
original references and pending analysis sources.

The Add warm probe also exposed a pre-existing native archive variation:
restoring the draft angle attaches three additional p-curves while published
viewer/reference fields remain equal. The eager baseline fails the same strict
archive comparison. A non-destructive Boolean experiment did not resolve it
and was removed. No Add warm timing is accepted from those failed probes.
The isolated cold Add sample decreased from 76,787 to 62,970 ms. Its geometry
digest `bd241c60d88ff5c9625df4c8ebedece55ed71e2236064dfdd28fddaf69312cbe`
matches the eager baseline exactly after excluding only analysis representation.
This check includes native archives, viewer geometry, properties and references.

## Verification

The dedicated face-demand test compares independent OCCT integration and an
analytic trimmed-plane equation, curved faces, transformed copies, nested
occurrences, save/reopen and read-only document revision. Comparing an older
original face's cached area alone is not an oracle for a currently trimmed face.

The selection GUI fixture exercises actual Tree and View mouse clicks,
collapsed/expanded features, explicit Sketch-child selection, double-click
inspection and Light -> Dark -> Light icon states. It checks unchanged document
revision and base-scene generation for ordinary clicks.

Factory native assets are recalculated, saved and reopened with the changed
implementation, preserving authored histories and references. Start templates
also receive the automated New Document GUI check. The local user-edited
`ZE-TEXT.symz` is preserved; its refreshed factory version is prepared separately
from committed input and must be included in a future committed release source.

The FORM Undo check now compares the exact pre-flatten native packet, then
explicitly inspects a private native copy for its volume equation. A deferred
scalar value is not evidence that Undo lost geometry. Aggregate analysis policy
is unchanged by this follow-up.

The performance and selection changes add no user-visible strings. The related
Assembly history command descriptions are translated in Czech, English, German,
French and Russian and checked by the catalog/coverage contract. These finite
Windows checks do not establish native Linux acceptance or eliminate the
remaining cost of edge publication, native archives and full save validation.
