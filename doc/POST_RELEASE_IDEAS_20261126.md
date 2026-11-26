# Ideas after the current Windows release

Recorded from the user on 2026-11-26. These are candidates for later work, not
additions to the current release or an approved implementation order.

- **Mechanical features:** splines, gears, grooves/undercuts and centering features.
  A provisional UI name is Mechanical Features; Mech/Pro is a naming idea.
- **Assembly mechanisms:** motion driven by Assembly constraints, preserving exact
  authored references, available degrees of freedom and immediate-owner rules.
- **Part surface meshing:** finish the existing work and verify a representative
  practical part. Confirm the precise remaining scope before implementation.
- **Libraries and engineering tables:** complete missing practical content and
  maintain the ordinary factory calculation, mass and native-document checks.
- **Standalone DXF-based 2D CAD:** investigate reuse of the Sketcher and interchange
  implementation as a separate future product scope.

Suggested order for discussion: finish the bounded surface-meshing work, develop
Mechanical Features in usable increments, then Assembly mechanisms. Libraries
can grow with the corresponding features. A standalone 2D product remains later.
The user has not selected this order.

## Explicitly deferred Sketcher idea

Take geometry would import a source Sketch into the current Sketch. Locked means
linked to the source; unlocking creates an independent editable copy. The user
explicitly deferred this command during the current release. The work-plane field
remains a plane selector. See the
[feasibility audit](verification/20261126-shared-sketch-input-audit.md).
