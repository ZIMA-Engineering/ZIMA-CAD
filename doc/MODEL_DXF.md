# Model and flat-pattern DXF

Model DXF is a manufacturing export in millimetres. It differs deliberately from
Drawing DXF, which retains the configured Drawing view and its annotations,
scale, cropping, sections and vector-output line styles.

| Layer | Color (ACI) | Contents |
| --- | --- | --- |
| CUT | White/black (7) | Ordinary profile geometry |
| BEND | Cyan (4) | Finite development bend axes, centre-line pattern |
| MARK | Green (3) | Stored text outlines for surface marking |

Generic construction geometry and helper axes are omitted from model export.
Text contours preserve their stored rotation, alignment and glyph shape without
requiring a matching font in the receiving system. Colors and layers distinguish
manufacturing intent; the receiving CAM system must assign the actual marking
and cutting operations.

The Sheet DXF command unfolds a private document copy when necessary. It reuses
the same persisted finite bend axes as Drawing views, projects them onto the
flat material's middle plane and clips them against the actual solid. Cuts and
holes therefore interrupt the exported axes. The active document, history and
formed state remain unchanged. This explicit export calculation does not add
kernel work to hover or interactive display.

Reimport recognizes BEND and MARK as construction geometry, preventing these
non-cutting entities from silently becoming solid profile curves. The existing
CONSTRUCTION input layer remains supported for external DXF files.

Drawing DXF continues through its independent shared GUI/CLI renderer. The
manufacturing layer/filter policy above does not apply to Drawing export.
