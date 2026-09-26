# ZIMA-CAD development rules

## Conservative changes to working code (mandatory)

- The project is at an advanced stage. Preserve working functionality and
  established contracts; change working code only when necessary for the
  requested task or a demonstrated defect.
- Before editing, trace the affected operation and its dependencies. Prefer
  the smallest coherent fix that addresses the cause and reuse existing
  implementations. Do not mix unrelated cleanup, renaming, refactoring or
  architectural changes into a feature or bug fix.
- When a broader change is necessary, explain its concrete reason and affected
  behavior. Existing requirements for explicit approval of protected shared
  contracts still apply.
- Verify the changed behavior and relevant existing behavior with checks
  proportionate to the risk, including dependent operations where affected.
  Do not claim preserved functionality without supporting verification; state
  any remaining uncertainty.
- This requirement was agreed with the user on 2026-09-24.

## Performance without behavioral regression (mandatory)

- Optimize unnecessary work, repeated calculations, allocations and data loading
  while preserving functionality, geometric accuracy, model properties,
  reference identity, persistence and Undo/Redo behavior.
- Never obtain speed by silently reducing calculation precision, display
  correctness or supported behavior. Reuse cached calculations only when their
  actual inputs remain valid; changed source geometry must invalidate reuse.
- Measure reported slow operations on representative user data and verify
  equivalent results as well as improved timings. Confirming unchanged
  properties must not regenerate geometry or create an undo transaction.
- This requirement was agreed with the user on 2026-09-23.

## Interactive drawing performance (mandatory)

- Ordinary interactive drawing views must use the already calculated 3D viewer
  geometry and depth-tested rendering, with continuous gray hidden edges. Apply
  hidden-edge dashes only for printing and vector export. While placing a new
  ordinary or projected view, show only its bounding frame with the arrow cursor;
  keep creation properties lightweight and prepare ordinary new-view geometry
  on OK. If no source extent is already available, use a provisional frame
  instead of loading the model just for placement. View selection changes only
  its bounds overlay, never the model stroke colors. Do not
  require precise vector hidden-line subdivision before displaying, placing,
  panning or zooming a view.
- A projected view inherits its parent's source, scale and display settings.
  Placement click confirms and creates it directly without opening Properties;
  Escape cancels before insertion. Properties remain available for later edits.
- Calculate precise output strokes when PDF export, printing or another vector
  export requires them. Keep output preparation separate from interactive
  rendering; a display image must never replace persisted reference geometry.
- Preserve sections, breaks, details, cropping, dimensions, snapping, picking,
  source/occurrence identities and save/reopen behavior. These operations must
  continue to use their real geometry and established coordinate conventions.
- This is the target architecture agreed with the user on 2026-09-23, not a
  claim that the existing drawing renderer already implements it.

## Documentation language (mandatory)

- All project documentation must be written and maintained in English only.
- This applies to new and existing documentation, READMEs, manuals, roadmaps,
  design notes, development handoffs, and documentation added with code changes.
- Translate non-English documentation when updating it; do not introduce or
  maintain parallel Czech documentation. Preserve technical meaning, status,
  examples, identifiers, file paths, and working links.
- Literal localized UI strings, Unicode test data, proper names, and third-party
  legal notices may retain their original spelling where exact text is required;
  explanatory prose around them must be English.
- This is a binding user requirement agreed on 2026-09-15. It does not change
  the language of conversations with the user or the application's localization.

## Localization with every change (mandatory)

- Every change must include a localization review. New or changed user-visible
  text must be translated into every supported language: Czech, English, German,
  French and Russian (`cs`, `en`, `de`, `fr`, `ru`) in the same change.
- This includes menus, toolbars, Tree labels and context actions, dialogs,
  tooltips, status messages, validation messages and user-visible errors.
- Reuse shared translation keys; never hard-code localized UI text or leave
  source-language placeholders in another language's catalog.
- Preserve formatting placeholders, units, shortcuts, identifiers and file
  extensions. Do not translate user-authored names or persisted model content.
- Run localization coverage and catalog validation, and verify affected UI text
  after changing language. A UI change is incomplete until its localization is
  complete. Changes with no user-visible text still require checking that fact.
- This is a binding user requirement agreed on 2026-09-21.

## Local development launch (mandatory)

- The user's normal Windows development entry point is `zima-cad.bat` in the
  repository root. Keep this name and location stable across builds and releases.
- It launches the current local C++ build with the repository working directory.
  Maintain this launcher when build paths change; do not require the user to
  navigate build directories or change their normal launch path for each release.
- Versioned portable packages use their own installation-root launcher. Creating
  a release must not redirect the development BAT to an older packaged version.
- This workflow was confirmed by the user on 2026-09-15.

## Drawing and title-block coordinate convention

- Drawing frames and title blocks intentionally use horizontal coordinates
  measured from right to left. This is an engineering-sheet anchoring convention,
  not an accidental mirror transform to remove.
- Preserve the title block's right-hand anchor when changing the drawing frame
  or sheet format. Changing the frame must not shift the title block away from
  its intended position.
- Library symbols may use conventional local XY coordinates. Convert them at
  the insertion/rendering boundary while preserving the drawing convention,
  symbol handedness and readable text. Do not reverse the document coordinate
  system to accommodate a library asset.
- Verify frame changes and symbol/text orientation when modifying this area.
  This requirement was reaffirmed by the user on 2026-09-22.

## Property and parameter dialogs

The application must present one consistent visual and interaction style.
Equivalent actions must use the same controls, terminology, layout conventions
and interaction behavior across workspaces. New work must reuse established
shared components rather than introduce command-specific styles. Apply this
incrementally within the requested scope, preserving working behavior.

Resizable properties windows keep controls anchored from top to bottom with
stable vertical spacing. Additional height belongs below the content; horizontal
growth may expand text/reference fields while compact controls keep their size.
Lower editable tables and lists are an intentional exception: where additional
visible items are useful, they consume surplus height above the bottom actions.
Their row heights stay fixed; only the viewport grows. Upper fields remain
anchored. The shared properties-window implementation owns this layout policy.

### Shared reference-entry template (mandatory)

- New reference-entry fields must reuse the shared controls in
  `zima/ui/reference_cell.hpp`, following the appearance and interaction of
  container placement reference entry. Do not build independent lookalikes.
- Use the shared green-arrow/clear indicator, `ReferenceCellItem` and its
  delegate, and the independent inspection eye. Only the active input field has
  a green outline; inspection uses the existing azure state. Clicking the field
  arms selection or replacement. Clearing a reference removes its inspection
  and reactivates entry; a short middle click ends entry without deleting data.
- Keep indicator and eye columns compact and fixed-width; let the reference
  text column stretch. Anchor the form at the top and put additional window
  height below the fields, rather than distributing it between input rows.
- Keep a separate row-number column, as in container placement. Row numbers
  express display order, not persistent identity; retain semantic point/station
  labels in their data columns. Show placement status beside the degrees of
  freedom in the lower status row, with status on the right.
- In reorderable point/item tables, a checkbox before the remove/arrow column selects
  exactly one item for ordering. Reordering moves that item by
  one position and keeps the selection attached to its identity. Disable moves
  beyond the first/last position and moves when no valid item is selected.
  Clicking an editable object field opens its editor with one click; clicking
  a reference-input field arms input instead. The independent eye only inspects
  geometry. Do not use field clicks or inspection to implicitly change the
  ordering selection. Apply this interaction contract to other containers
  only where reordering is meaningful, preserving their existing capabilities.
  Use bottom Up/Down buttons with the shared green arrow icon before localized text.
- Consistent presentation must preserve each field's capabilities. Show clear
  only for removable references; required replaceable references offer
  replacement without clear. Derived read-only fields offer neither input nor
  removal. Keep alignment with an empty control slot where appropriate. Show
  inspection only where exact geometry can be inspected. Never add deletion,
  replacement or editing merely to make controls look uniform.
- Before changing a clear/remove control, identify whether it clears a field,
  deletes a list item and its row, or removes an explicit definition while
  retaining its structural row (for example a Sweep station inheriting a
  profile). Preserve that meaning and describe it accurately in the tooltip.
  Review existing commands individually; visual consistency does not authorize
  a global rewrite of their deletion semantics.
- Offer only controls meaningful to the command. A source-face-only command
  does not need an Origin button. Consume shared components without changing
  the protected container-placement contract.
- Verify selection, replacement/removal, independent inspection, Cancel and
  resizing as applicable; localize all labels and tooltips in all five languages.
- Apply this template to new work and explicitly requested updates. Do not
  rewrite working existing dialogs merely to enforce visual uniformity.
- This requirement was agreed with the user on 2026-09-24.

- Every newly created property, feature-parameter, or editing dialog must use
  the same in-application `Qt.WindowType.SubWindow` presentation and visual
  style as the existing Container Properties window.
- Such dialogs must be parented to the main application window, remain inside
  its bounds, use the shared internal properties title/style conventions, and
  be positioned with the existing properties-window helpers.
- Do not introduce free-floating native/system dialogs (`QInputDialog` or a
  top-level `QDialog`) for feature properties or editable model parameters.
- Reuse or extract the shared properties-dialog implementation instead of
  independently recreating its window flags, styling, movement, confirmation,
  and placement behavior.
- Creation and later editing of the same feature must use one dialog class and
  one interaction contract. Do not maintain separate "create" and "edit"
  property windows; inject only the operation callbacks and initial values.
- Property and feature dialogs expose only `OK` and `Cancel`; do not add or
  retain an `Apply` action or an intermediate Apply transaction. `OK`
  validates, calculates, commits, and closes. `Cancel` closes without
  committing the pending dialog changes.
- Measurement and Body measurement are inspection exceptions explicitly agreed
  with the user. Their separate **Save** action commits the record and closes;
  OK, middle-button double-click, Cancel and the close button only close without
  saving pending changes. Opening an inspector never inserts a history row.
- A middle-button double-click invokes OK even while the pointer is over the
  3D view. A short middle-button click does not commit a dialog. Middle-button
  drag is reserved for view navigation and must not confirm a dialog.
- This middle-button double-click contract applies to every in-application
  dialog that exposes an enabled `OK` action, not only feature/property
  dialogs. Dialog classes must use the shared confirmation mechanism. A class
  may opt out only when it implements the same behavior itself, including
  confirmation while the pointer is over the owning document view, and has a
  regression test for that equivalent behavior.

## History container editing

- Rollback is a general container-editing rule, not a Fillet-specific feature.
  Opening Properties for any history container evaluates and displays the model
  at the boundary immediately before that container. The edited container
  remains visible in the tree with its active azure background; downstream containers
  are suppressed only for the edit session.
- Pending edits and creation previews remain transient. Only OK updates or
  inserts the history container; Cancel restores the unchanged input/history.
- During rollback the 3D view shows the real input geometry before the edited
  container, never the cached final body. Picking resolves against this input.
  A transient preview may replace it while the dialog is open; OK or Cancel
  restores normal full-history display, selection mode, and highlights.

## Container placement protection

- Every placed history container must opt into the complete shared placement
  contract through the single common capability predicate used by both its
  Properties dialog and the View. Do not maintain separate feature lists for
  the dialog and viewer. The contract includes position and FRONT/TOP
  orientation references, whole-Origin entry, reference inspection and
  replacement, degrees-of-freedom state, numeric correction, persisted
  placement, and the shared cyan wire preview without an OCCT calculation.
- A new placed container is incomplete until GUI tests cover creation,
  reference and whole-Origin placement, OK persistence, reopening for edit,
  and Cancel restoration.
- Do not modify the shared container-placement code or its general placement,
  reference-solving, orientation, offset, preview, or persistence contracts
  without first asking the user and receiving explicit approval.
- Feature work must consume the existing container-placement contract. Do not
  alter that shared contract merely to repair or specialize one feature.
- If a requested change appears to require changing container placement, stop,
  explain the proposed change and its possible effects on other containers,
  and ask the user before editing it.

## Explicit dependency regeneration

- A source Part owns its current calculated geometry. Assemblies share and
  display that current source data; they must not pin historical Part revisions.
- Switching tabs must not invoke OCCT, recalculate mates or regenerate Assembly
  operations. Displaying already calculated source data is not regeneration.
- Changes to calculated Part geometry must become visible in its Assembly
  occurrences without requiring Assembly regeneration just to refresh display.
- Mate solving and Assembly-owned body operations (such as cuts) run only on
  explicit Regenerate. Open source documents are authoritative; saving them
  first is not required.

## Assembly editing ownership

- Assembly Extrusion and Revolution always subtract material from selected
  immediate Part occurrences. Creation, editing and conversion of a standalone
  Sketch must never add material. GUI and CLI enforce this same rule.

- Every component is positioned only by its immediate owning Assembly. A
  parent Assembly treats an inserted subassembly as one component and must not
  directly own mates that position the subassembly's internal components.
- Activating a Part or Assembly changes the writable editing document to that
  component's source document. A Part exposes Modeling tools; a subassembly
  exposes Assembly tools and those tools operate only on its own immediate
  components.
- Activation never replaces the displayed top-level Assembly. The complete
  top-level scene remains visible as passive context while tree editability,
  hover, confirmed selection and commands follow the exact active occurrence,
  even when that Part or Assembly is nested several levels deep.
- Geometry outside the active document may be exposed only as an explicit,
  read-only external reference. Such a reference does not transfer ownership
  of the referenced object or permit a higher Assembly to drive internal
  subassembly placement.
- Dependency edges are one-way. Creating an insertion or external reference
  that would introduce a direct or indirect dependency cycle must be rejected.

## File-format compatibility

- All persistent information required by a Part, Assembly or Drawing must live
  exclusively in the native `.prtz`, `.asmz` and `.drwz` document files.
  Dependencies may refer to those native documents. Do not introduce required
  external geometry, revision, sidecar or cache files/directories (including
  `.zima-revisions`). In-memory sharing and disposable derived caches must never
  be the sole storage of information required to reopen the native documents.

- Keep the existing document extensions. Every format change must update the
  corresponding start Part and Assembly templates under `config`; keep all of
  `config`, including those templates, tracked in Git.
- A native Part or Assembly format change is incomplete until the corresponding
  start template has been regenerated with the changed code and an automated
  GUI check has created a new document from that template, confirmed its first
  editable Body/component context is active, and confirmed its normal commands
  are enabled. Do not rely only on direct `create_default()` or load/save tests.

- Backward compatibility with legacy Part and Assembly files is not required.
  This includes old `.prt`, `.prtz`, `.asm`, and `.asmz` documents.
- Do not add migration branches, legacy topology fallbacks, compatibility
  adapters, or duplicate old/new execution paths for those formats.
- Prefer the simplest, fastest, and most reliable current data model even when
  that intentionally makes legacy documents unsupported.
- When redesigning topology or serialization, remove obsolete compatibility
  code instead of preserving it behind conditionals.

## Windows runtime and release packaging

- Before changing or publishing a Windows runtime or portable build, read and
  follow `doc/WINDOWS_RUNTIME_AND_BUILD.md`.
- C++ is the only product implementation. Do not restore the removed Python
  application or package its Conda environment as the product runtime.
- Each release must carry its own native Qt/OCCT dependencies, plugins and
  resources. Resolve the actual dependency closure for the selected platform;
  do not require OpenBLAS merely because the former Python runtime used it.
- Use repository-owned packaging and validation scripts. Do not use PowerShell
  `Compress-Archive` or `Expand-Archive` for the runtime/build tree, and do not
  bypass archive path, collision or member-length checks.
- Build release ZIPs from committed Git data in a short staging directory.
  Never allow untracked working files into a release and never replace a
  known-good archive until dependency, path, CRC, SHA-256 and smoke checks pass.
- The versioned distribution requirements in `doc/DISTRIBUTION_CLEANUP_PLAN.md`
  are binding. Use build IDs `YYYYMMDDNN`, product-specific tags/archive names,
  independent native dependencies/resources per version, complete source and
  portable user data outside version directories. Follow its validation gates.
- Linux completion belongs on the Linux host. Read `doc/LINUX_RELEASE_HANDOFF.md`
  before continuing it. The obsolete runtime tree has been removed after
  Windows verification; establish and verify fresh native Linux dependencies.
- The root `VERSION` file is the single build identity. Shared user settings
  belong in installation-root `config/` (with Windows/Linux overrides); factory
  settings belong to each immutable version. Preserve configuration and projects
  during updates. Read `doc/NATIVE_DISTRIBUTION.md` for implementation details.
- Do not claim an updater, signed release pipeline or portable layout is
  implemented before it has been verified.

## Geometry side identity (mandatory, permanent modeling contract)

- The program distinguishes which side of geometry a reference or contact
  uses, including at zero offset. Never remove, merge or reinterpret these
  side identities in later refactoring, optimization or format changes.
- Internal representations may change only while preserving the same visible
  behavior, geometric result and persisted side choice. Preserve the choice
  through creation, editing, save/reopen, regeneration and Undo/Redo.
- Read `doc/GEOMETRY_SIDE_CONTRACT.md` before changing signed offsets,
  oriented normals, side flags or numerical cache normalization. Signed zero
  is meaningful in Bend endpoint dimensions; never normalize it globally.
- Tests must distinguish the two sides and cover zero offset whenever the
  affected operation supports it. Numeric equality alone is not proof of
  equivalent modeling intent. This is a binding user requirement reaffirmed
  on 2026-09-19, not an optional implementation preference.

## OCCT boundary

- Use OCCT only as the solid-modeling kernel for calculating body geometry.
- Topology ancestry is a mandatory persisted parent-child relation, not a
  display label and not reuse of one ID for two objects. For Extrusion and
  Revolution, a generated side face is the child of its source Sketch curve;
  a generated sweep/longitudinal edge is the child of its source Sketch point;
  start/end rim edges are children of their source Sketch curves; start/end
  vertices are children of their source Sketch points; and start/end cap faces
  are children of the selected profile region. The child identity contains the
  feature ID, semantic role and parent source ID, so its parent can always be
  recovered from persisted ZIMA data.
- Never create persistent topology identity from OCCT enumeration position,
  traversal order, `FirstShape`/`LastShape` naming assumptions, or synthetic
  keys such as `face:N`, `edge:N`, or `vertex:N`. OCCT history may locate the
  runtime shape for a ZIMA identity that was defined before calculation; it
  must never define that identity.
- Application UI, property dialogs, Sketcher entry and interaction, picking,
  highlighting, topology identity, feature references, external sketch
  references, and their projections must use ZIMA-CAD's persisted data model
  and viewer data, not live OCCT traversal or reconstruction.
- Resolve and persist all reference data needed by later editing when a body
  calculation is explicitly performed. Opening or editing an already
  calculated feature must consume that persisted data without invoking OCCT.
- Do not hide OCCT work behind refresh, selection, overlay, tree, toolbar,
  properties, or hover code paths. A user action may invoke OCCT only when it
  explicitly requests a body calculation such as Apply, OK, or model
  regeneration.

## Viewer selection contracts

- Feature dialogs using the shared Origin action must not automatically expose
  every Part/subassembly Origin in the displayed Assembly. Offer the top-level
  Assembly Origin by default; reveal other Origins only when explicitly
  requested through Origin selection. This display state belongs to the exact
  occurrence path, never all instances of a source document. Hidden Origins
  must be absent from the common picker as well as from painting. Closing the
  dialog retires this temporary visibility policy without changing references.

- Ordinary LMB selection follows one general rule in every workspace: clicking
  a valid offered candidate confirms exactly that candidate and synchronizes
  the Tree; clicking empty View space clears the confirmed View and Tree
  selection together and removes selection/inspection overlays. An active
  command may give an empty click another explicit command-local meaning, but
  must never leave an unrelated stale confirmed selection behind.
- Hover, left-click confirmation, and pre-confirmation right-click cycling must
  consume one common ordered candidate list produced by the viewer. An active
  command may filter that list through its selection contract, but must not run
  a parallel picker, use different hit tolerances, or recompute a different
  candidate on click. Before confirmation, RMB changes only the active index in
  the same list.
- Every active command owns an explicit viewer selection contract defining
  what is displayed, offered on hover, accepted on click, and persisted.
- With no active command, selection is leaf-first and consistent in every
  workspace. Assembly hover offers the lowest concrete Part occurrence under
  the pointer, not an undifferentiated nested Assembly; Part offers individual
  history containers. Hover uses the green wire and LMB confirms the exact
  candidate with the azure wire; ordinary result-body topology is not offered.
- RMB over an LMB-confirmed object opens its context menu instead of cycling.
  The context menu exposes **Select Parent** whenever the selected object has a
  selectable parent. Each invocation moves selection exactly one hierarchy
  level upward, may be repeated through arbitrarily nested Assemblies, and
  synchronizes the tree and view to the same selected instance path.
- RMB cycling remains active before LMB confirmation and throughout an active
  command such as Assembly mating. Active commands do not open the ordinary
  object context menu until they finish or cancel.
- Every occurrence in a nested hierarchy has its own stable instance path.
  Hover, confirmation, context actions, Select Parent, activation, visibility,
  persistence and highlighting must distinguish repeated occurrences of the
  same source Part or Assembly. Names and source entity IDs alone are not
  occurrence identity.
- Stable feature, container, Sketcher, and Assembly-mate references use the
  persisted faces, edges, vertices/points, axes, and planes of original
  objects/solids. Result Part/Assembly body topology and transient previews
  are not valid reference owners.
- Fillet and Chamfer are explicit exceptions: they select edges of the real
  input body at the operation boundary. This operational body selection is
  not a general persisted placement reference.
- Sketch External Geometry is a separate exception agreed on 2026-09-23:
  it projects the actual calculated body edge, including trimmed edges and
  edges created by Fillet/Chamfer. Preserve an inherited original identity
  where available; never substitute the untrimmed source geometry. Persist
  this source choice and follow the same body boundary on regeneration.
  Ordinary Sketch external references still use original objects, including
  original faces. This exception does not change container placement.
- Highlight only the exact candidate geometry. Do not colour, tint, or add a
  coloured overlay for an entire body when offering a topology reference.
- Reference-entry controls expose two independent visual states through one
  shared implementation. Exactly one reference field in the active dialog may
  own input at a time; draw that field with a green outline and never express
  input ownership through `QTableWidget` selection. Zero or more already stored
  references may be inspected independently; draw only their exact fields with
  an azure background and highlight only their exact persisted geometry in the
  View. An eye control toggles inspection and clicking the reference text arms
  input/replacement. A short middle-button click ends reference entry, removes
  the green outline and clears temporary inspection highlights without deleting
  pending reference values. Adjacent cells must never inherit either state.

## Engineering Reasoning

When working on engineering, CAD, geometry, mechanism, manufacturing,
or design-related tasks in this repository, read and follow:

`doc/AI/ZIMA_ENGINEERING_REASONING.md`

This document defines the engineering reasoning methodology used by ZIMA-CAD.

It is not optional background documentation.
Treat it as the primary reasoning framework for engineering design tasks.

### Core Engineering Model

Always reduce an engineering problem to:

INPUTS → MEANS → OUTPUTS

Before proposing a detailed solution:

1. Identify the inputs.
2. Identify the required outputs.
3. Identify the available means.
4. Perform an independent measure / sanity check.

Means include manufacturing means.

Do not assume that dimensions, masses, forces, powers, speeds, costs,
or other values supplied by the user are automatically reasonable.

Use engineering knowledge to independently check their scale.

### Existing Knowledge

Use known mechanisms, existing designs, standards, materials,
manufacturing methods, and previous solutions as engineering knowledge.

However:

DO NOT EQUATE THE MOST COMMON SOLUTION WITH THE BEST SOLUTION.

Existing knowledge should:

- provide scale,
- provide references,
- provide proven principles,
- expose known limitations,
- help verify candidates.

It must not unnecessarily restrict invention to statistically common designs.

### Solution Search

Prefer a simple proven solution when it satisfies the requirements.

Do not create novelty merely for novelty's sake.

If no satisfactory known solution exists:

1. combine known principles,
2. evaluate the result,
3. modify the concept,
4. evaluate again.

All evaluation must return to:

INPUTS → MEANS → OUTPUTS

### Escaping a Bad Solution Region

If conventional modifications repeatedly fail, do not endlessly optimize
the same concept.

Allow the search to leave the current solution region.

Generate alternative candidates that may significantly differ from the
current design.

These candidates may originate from unusual combinations, deviations,
unexpected associations, or deliberately introduced exploratory errors.

A candidate is not accepted merely because it is novel.

### Error and Invention

During exploration, error can be useful.

An unexpected or initially incorrect state may reveal a solution region
that conventional reasoning would not reach.

Therefore:

ALLOW ERROR DURING DISCOVERY.

DO NOT ALLOW ERROR DURING VERIFICATION.

Or equivalently:

"During invention, allow mistakes. During verification, do not."

Once an unusual candidate has been generated, immediately return to
rigorous engineering evaluation.

### Local Exploration

When an unusual candidate appears promising, explore its neighborhood.

Test at least three useful independent directions where practical.

Determine whether movement in those directions:

- improves the concept,
- degrades it,
- has little effect,
- or causes the principle to fail.

Do not assume that a working solution is a single point.

Try to understand the multidimensional region in which the principle works.

This region is its engineering MEASURE.

Multiple disconnected valid regions may exist for the same problem.

### Verification

Creative generation and engineering verification are separate phases.

During verification, use appropriate deterministic methods whenever possible:

- calculations,
- geometry checks,
- CAD kernel results,
- collision detection,
- kinematics,
- tolerance analysis,
- strength calculations,
- simulations,
- manufacturing checks,
- assembly checks,
- testing.

Never present an invented assumption as a verified engineering fact.

If verification is incomplete, explicitly state what remains uncertain.

### ZIMA-CAD Architecture

Do not unnecessarily couple engineering reasoning to a particular geometric
representation.

A design may exist as:

- parametric ZIMA-CAD objects,
- direct geometry,
- imported geometry,
- B-Rep / solid geometry,
- or an AI-generated intermediate representation.

The engineering intent and the geometric representation are related,
but they are not the same thing.

Preserve this separation when designing new ZIMA-CAD functionality.

### AI and Human Interaction

The AI should act as an engineering collaborator, not merely as a command
generator.

When interaction data is available, consider:

- selected objects,
- hovered objects,
- cursor position,
- cursor trajectory,
- spoken instructions,
- model geometry,
- parameters,
- engineering metadata,
- manufacturing constraints.

Words such as:

- this,
- here,
- there,
- these,
- move this,
- make this larger,

must be resolved against the current CAD interaction context whenever possible.

Do not guess a geometric reference when the available context is ambiguous.

### Engineering Knowledge vs. Engineering Reasoning

Keep these concepts separate.

ENGINEERING KNOWLEDGE answers:

"What do we know?"

ENGINEERING REASONING answers:

"How should we search for a solution?"

ZIMA-CAD should eventually support both.

Engineering knowledge may grow through databases, documentation,
standards, previous projects, catalog data, calculations, and experience.

The reasoning method is defined primarily by:

`doc/AI/ZIMA_ENGINEERING_REASONING.md`

### Final Principle

Knowledge is used to understand and verify reality.

It must not become a prison that restricts engineering invention to the
average of previously known solutions.

When a conventional solution works, use it.

When it does not, search elsewhere.

When searching elsewhere, allow mistakes.

When verifying the result, do not.

## Next-session development reminder

When the user next resumes ZIMA-CAD development, read the section
**Agreed next steps for Part (2026-09-06)** in `ROADMAP.md` and briefly
respect the 2026-09-19 scope correction: native Sketcher offsets are already
implemented and STEP-derived offsets are not required. Do not remind the user
to implement them again. Current Part bug fixes take priority. Preserve the
remaining listed order; the comprehensive Undo/Redo audit comes
only after the agreed modeling features are broadly implemented. This is a
reminder and planning preference, not authorization to implement the whole
roadmap automatically. New user instructions take precedence.

## Shared interaction feedback

- Reference-entry cells use light-green hover feedback on the exact enabled cell only. Leaving restores its stored inspection appearance; the active green input outline is independent. New point actions use green text on the ordinary background until hovered.
- Tree hover uses light green while confirmed selection retains its distinct appearance.
- Standard OK and Cancel buttons retain their localized labels and behavior, with the existing Sketch finish checkmark and a red cross respectively. Use the shared application icon installer rather than per-dialog copies.

- Use the interaction green #4DD811 for reference-entry outlines, entry arrows and command hover, matching Sketch/New point actions. Keep inspection cyan and confirmed-selection styling distinct. Command hover must follow the actual pointer and clear after leaving or opening a command; it must not become a persistent selected state.

- The interaction colour contract agreed on 2026-09-24 is defined in `doc/UI_VISUAL_CONVENTIONS.md`. Active GUI and Tree backgrounds are azure; hover is green. Confirmed selections, inspected references and pending feature wire are always azure, independent of add/subtract/surface operation. Sketch construction stays orange, model/Drawing axes brown, and Origin icons red. Do not reintroduce active green Tree text or orange hover.

## Native GUI color override (2026-09-26)

The current user agreement supersedes earlier green/azure presentation rules
above. Ordinary Qt Tree selection, tabs, menus and button feedback follow the
platform style. Explicit application states use azure for View hover/reference
entry and green for confirmed geometry, independent inspection, preview wire
and active/editing Tree rows. Preserve all existing state ownership and behavior.
Confirmation checkmarks remain green on hover; do not substitute black icons.
See `doc/UI_VISUAL_CONVENTIONS.md` for the current presentation contract.

Neutral UI icons and ordinary reference labels follow the current Qt text
palette in both light and dark themes, including existing icons after a theme
change. Tree Insert here markers use red. Semantic icon colors remain explicit.
