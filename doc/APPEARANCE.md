# Colors and appearance

**Colors and Appearance...** is available through the colored-sphere icon above
View or the color menu. It uses the shared internal Properties window with OK/Cancel.
It initially opens at the right edge at 600 x 760 logical pixels, constrained to
the main window. Palette spheres retain their size with tighter spacing.

## Palette and surfaces

The palette selector groups Basic Colors, Plastics, Paints, Metals, and custom
classes, retaining existing shades. Metals include steel, matte/polished stainless
steel, aluminum, matte/polished bronze, brass, and copper. The preview sphere uses
the model renderer. Enter colors as `#RRGGBB`; sliders control gloss and metallicity.
Every appearance also has a transparency slider and numeric percentage: 0% is
opaque and 100% fully transparent. The basic Skeleton shade is muted purple with
70% transparency. It is a palette preset, not a physical material.

For a custom appearance, enter a name, adjust the surface, choose/type a class,
and click **Add to Palette**. Custom entries are written to `appearances.json`
beside application settings only on OK; Cancel does not save them.

## Body and face groups

**Body - Base Appearance** selects the active body's base surface, or the whole
Part if no body is active. **+ Group** creates a named face group. Its color field
selects appearance; the face-count field arms input with a green outline. Clicking
View assigns visible **result faces**. A face belongs to at most one group; a new
assignment moves it. The eye independently highlights exact assigned-face outlines
in cyan. A short MMB click ends input and inspection without deleting values.

**Clear Faces** removes assignments but retains groups. **Defaults** restores the
edited body's base appearance and removes its groups, preserving the custom palette.
OK commits the complete change; Cancel restores the original appearance. MMB
double-click confirms OK even over View.

In Assembly, an immediate Part's selected appearance belongs to its occurrence.
It does not overwrite the source Part and survives explicit Regenerate. Activate
the Part first to edit the Part itself.

## Display and data

Surfaces use color, roughness, metallicity, direct specular highlights, and procedural
studio lighting with two soft reflections. This is neither ray tracing nor reflection
of surrounding components. Appearance does not assign physical material or density.

Groups store ZIMA result-face identities in Part; occurrence overrides are stored
in Assembly. These are presentation references, not construction-reference ownership.
Transparency is stored in the existing ARGB color value in native documents and
custom palettes. Opaque surfaces establish depth first; transparent triangles
are blended from back to front without writing depth. Edges remain readable.
Opening the window, selection, sphere preview, and appearance changes invoke no
OCCT or parent regeneration. After geometry changes, appearance applies only to
surviving identities; missing faces are not reassigned by order or proximity.

## Verification

`zima_cpp_appearance_contract_tests` covers Part/Assembly/palette serialization,
Assembly result-face selection, exclusive group assignment, Clear/Default,
OK/Cancel transactions, MMB double-click, and matte/metallic rendering differences
using an actual OpenGL framebuffer. Transparency checks cover slider/numeric
synchronization, custom palette persistence, Cancel restoration, mesh-order
independent blending and non-occluding fully transparent surfaces.
