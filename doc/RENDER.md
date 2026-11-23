# Render

Render creates an image of calculated Bodies in a Part. A Render Body owns
the scene settings, cameras and lights. It does not create or change material.
Available in [Windows 2026112301](releases/2026112301.md). See the
[design and acceptance scope](RENDER_BODY_DESIGN.md) and
[finite verification](verification/20261123-render-and-hole-properties.md).
The interaction refinements below are in the current development follow-up;
their [acceptance record](verification/20261123-offset-render-straighten.md)
tracks verification separately from release publication.

## Create a scene

1. Calculate the source Bodies and choose the desired initial View.
2. Activate the Part root and choose **Create Render**.
3. Keep **Visible Bodies**, or choose **Selected Bodies** and select the source
   Bodies in the Tree or View. Set the image dimensions and sampling quality.
4. Confirm with OK. The new Render Body becomes active and exposes Render tools.

Visible mode includes available material results preceding the Render Body in
history. It omits hidden/suppressed Bodies, consumed Boolean inputs and FEM or
Render Bodies. Explicit selection retains the selected identities independently
of ordinary visibility; it can include a consumed input or a Boolean result.
All selected Bodies share shadows, reflections and lighting in one image.

The initial camera follows the current orthographic or perspective View. The
initial background copies the View gradient. Source colors, opacity, roughness
and metallic finish come from Modeling. Render settings can override roughness
and metallic finish for the whole scene while retaining source colors.

## Cameras and lights

Use **Camera**, **Point light** or **Sun** to add a placed scene object. These
commands use the ordinary internal Properties window and shared placement
controls: reference entry/replacement, independent inspection, position,
orientation, corrections and whole-Origin placement. The default Origin comes
from the first selected source's material Body; other source Origins can be
selected explicitly. Missing references retain the last valid frame for repair.

A perspective camera uses its field of view. An orthographic camera uses its
frame height. Choose the active camera in Render Body properties. Deleting that
camera leaves the scene awaiting an explicit replacement.

A point light has a position, luminous power in lumens and radius in millimetres.
A sun has a direction, illuminance in lux and angular radius in degrees. Radius
controls soft shadows. Disabling a light removes its illumination; hiding its
View marker only hides the editor marker. Local +X is the camera/sun direction
and local +Z is camera up.

Single-click selects an object. Double-click exposes its position annotations;
Properties are available from the context menu. Creation and editing use the
same window. Cancel discards pending edits; unchanged OK adds no Undo step.

Camera and light markers, including their adjacent labels, use dimension yellow
in ordinary display. Hover and selection retain the shared interaction colors.
Choose **View from camera** in Camera Properties or its Tree/View context menu
to see through that camera. Pending camera parameters can be inspected this way
without committing them. This changes the View, not the camera definition.

## Calculate and export

Choose **Render image**. The command captures the calculated Bodies, appearance,
camera, lights and quality settings. A private background worker renders that
snapshot while you can switch back to the model and continue working. Editing
the source does not restart or cancel the job. A completed earlier state is
labelled as a historical snapshot, and can still be retained or exported.

The separate **Render** tab displays the result and progress. **Render gallery**
in the right command toolbar reopens it when a job or retained images exist.
Closing the tab leaves the computation running; **Stop rendering** cancels it.
Closing the owning document stops its job. The current implementation runs one
job at a time, with at most four workers and at most half the available logical
CPUs (at least one). It publishes previews at most four times per second. It
never evaluates CAD geometry or refreshes the model scene for progress updates.
These bounds reserve capacity; interaction performance still requires measurement
on representative hardware and models.

More samples reduce noise; additional bounces permit more indirect illumination
and reflections. Exposure changes lit objects while preserving the background.
Use the mouse wheel to zoom around the pointer, with the same direction and
steps as Drawing. Hold the middle and right mouse buttons together to pan.
Commands are in the usual right-hand column; image selection is on the left.

Export the selected completed image with **File > Export (F7)** or **Export** in
the gallery. Choose JPEG (`.jpg`/`.jpeg`) or PNG (`.png`). The output has the
configured dimensions, without View axes, markers or selection feedback. Export
cancellation leaves the document unchanged. The shared atomic image writer
preserves an existing output if encoding or replacement fails.
Image Export is available in the Render image tab, not among the scene's
modeling commands.

## Retained gallery inside a Part

**Save to Part** explicitly adds the completed snapshot to the Render Body's
embedded gallery. It does not automatically save every render. Several images
can be retained or exported separately. Right-click a saved image in the left
gallery to rename or delete it; Rename opens its inline name editor. Retaining,
renaming and deleting are ordinary Undo/Redo transactions. The normal Part Save
stores them inside `.prtz`, including small thumbnails; no external image file
is required to reopen the gallery. Retained images are historical records and
do not change when the model changes.

Full-resolution images use lossless PNG compression. Undo and document copies
share immutable compressed buffers. Opening the gallery loads small thumbnails;
a full image is decoded only for display/export. Geometry and solving never
consume these images. File size and save/open work increase with the number and
resolution of retained images. Deleting images reduces the next saved document;
Undo may keep their shared buffers alive until its history is released.

## Initial limits

The first implementation renders calculated solid Bodies in Parts with a shared native
CPU ray renderer. Assembly scenes, animation, textures, depth of field, glass
refraction and denoising are separate work. Surface opacity provides transmission
without glass refraction. Low sample counts can show visible noise. Missing or
invalid sources/placements must be repaired before rendering; the renderer does
not regenerate CAD geometry to repair them. Native Linux execution has not yet
been verified for this feature.
