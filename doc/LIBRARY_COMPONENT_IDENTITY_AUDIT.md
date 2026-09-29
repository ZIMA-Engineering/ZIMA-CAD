# Library component identity audit

Date: 2026-09-29. The initial audit below records the identity behavior before
the implementation follow-up at the end of this document.

## Requirements

Inputs are independent library files, Family Table members, repeated and nested
occurrences, and persisted references to original geometry. Required outputs
are unambiguous source geometry and editing ownership, reference-preserving
Replace when corresponding geometry exists, and the user's filename-based BOM
policy. Available mechanisms are native Save Copy, Family Table identities,
occurrence paths and semantic topology identities. The independent check uses
different source volumes while retaining the same local face reference.

The requested future option belongs to the component occurrence in its owning
Assembly. It affects the BOM only. Enabled occurrences use the source filename
without the variant; ordinary occurrences retain variant-sensitive behavior.
Different filenames must remain different BOM items, even for identical parts.
Tree labels and actual selected geometry must retain the selected variant.

## Current implementation

- `Workspace::add_part` and `add_assembly` require unique document IDs. A second
  file with an existing document ID cannot be added as a separate open document.
- An occurrence stores `source_path`, `source_document_id`, `occurrence_id` and
  placement references. A mate reference uses the full `InstancePath`, local
  `owner_id`, `semantic_key` and reference kind. It does not store a filename as
  its direct geometry identity.
- Family members use a document identity derived from the parent document ID
  and stable row ID (`:family:`). Local feature/topology identities are retained.
- Native Save Copy creates a new document ID, remaps document-owned identities
  and keeps local modeling identities. It is the existing suitable mechanism
  for creating an independently editable library copy.
- Replace retains the occurrence and stored topology references. It explicitly
  remaps immediate occurrence Origin ownership. Missing topology remains a
  repairable missing reference; it is not guessed from a nearby face. Replace
  with the same source document ID is a no-op.
- Tree labels already use the actual Family member name.
- `build_bom_rows_for_source` currently groups by document ID plus normalized
  path. The proposed filename-only option and grouping policy are not present.

## Duplicate document ID hazard

Cold Assembly hydration caches by path plus source ID, but the open-source
resolver uses maps keyed only by document ID. Its matching branch does not
compare the component's source path. Therefore different files carrying the
same document ID are not a supported independent-source workflow: opening one
can make its geometry authoritative for references to that ID. The duplicate
open rejection and same-ID Replace no-op reinforce this limitation. Do not
interpret successful cold loading alone as evidence that this workflow is safe.
This hazard was traced in code; no product behavior was changed by this audit.

## Recommended contract

Keep independent document IDs and occurrence paths. Share corresponding local
reference identities across related library copies/variants when they retain
the same modeling meaning. Resolve a reference through its occurrence to the
selected source document/member, then to its local owner and semantic key.
The file locates the source; it is not a replacement for document identity.
The filename-based BOM rule is independent of geometry/reference ownership.

An axis and a seating face can remain compatible across different screw sizes.
Suppressed, deleted or structurally changed reference geometry cannot be
promised to survive Replace. A detail variant must retain required references
if it is to be interchangeable. Arbitrary unrelated models and replacement
subassemblies with different internal occurrence paths are not guaranteed to
map automatically. Reusing an ID must never mean assigning it to unrelated
geometry merely to make a mate appear valid.

Before implementing the BOM option, cover mixed enabled/disabled occurrences,
variants and plain parts, nested assemblies, filename changes, duplicate leaf
filenames in different directories, balloons/item numbers, metadata/mass values,
Undo/Redo, and native save/reopen. A merged row must retain all occurrence paths;
the policy for variant-dependent values must be explicit. Keep these BOM rules
out of source resolution and placement solving.

## Verification

The existing nested-copy, component-source, component-command and Family Table
test suites passed. The new Family Table regression adds:

- rejection of duplicate document IDs under different file paths;
- Save Copy with a different document ID and retained local feature identity;
- replacement of an actual planar-face mate by the independent copy;
- distinct source volumes (480 and 240 cubic model units) without aliasing;
- Replace Undo/Redo and cold save/reopen with the reference retained.

The initial audit added only tests and this note. It verified the tested
related-copy scenario, not arbitrary library geometry or every topology kind.

## Implementation follow-up: occurrence BOM policy

Component Properties now exposes **Ignore variant in BOM**, localized in all
five languages. The default is off. The owning Assembly persists
`bom_ignore_variant` on the occurrence, independently for Parts and Assemblies.
`component.set` accepts the same boolean and component queries expose it.
Changing it alone uses the existing metadata transaction without requesting a
placement solve or body calculation. Cancel discards it; Undo/Redo restore it.

Checked occurrences group by the exact source filename, including its native
extension, without directory, document ID or Family row identity. Their shown
name is the file stem. Different filenames remain separate. Unchecked
occurrences retain the existing ID/path grouping, so an unchecked variant is
not absorbed into the checked group. Tree labels, variant identity, geometry,
references, and source files are unchanged. BOM regeneration uses the generic
source's parameter context for checked variants, avoiding input-order-dependent
detail-variant metadata. All grouped occurrence paths remain available to
balloons. Existing immediate-component BOM scope is preserved.

Two unrelated files with exactly the same filename can therefore merge when
checked, by the requested filename rule. The first encountered source supplies
the row metadata in that case; this option is intended for representation
variants of the same physical item. Source identity safety remains independent
of this presentation policy.
