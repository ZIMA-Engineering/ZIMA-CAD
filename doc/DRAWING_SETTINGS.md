# Drawing defaults and quick exports

Global Settings > Drawings edits the `[Drawing]` section of `config/config.ini`:

```ini
[Drawing]
ViewDisplayStyle=hidden_edges
PdfDirectory=pdf
DxfDirectory=export
```

`drawing.ini` is no longer used. New ordinary views use this display default;
projected views inherit their parent. Supported styles are `visible_edges`,
`hidden_edges`, `shaded_with_edges` and `shaded`. Existing views retain their
stored settings.

The drawing command toolbar ends with PDF and DXF buttons. Both paths are
relative to the saved drawing's directory. PDF writes all sheets to
`pdf/<drawing>.pdf`; DXF writes only the active sheet to
`export/<drawing>_<sheet-number>.dxf`. Repeated exports replace that derived
output. Save a new drawing before using quick export. File > Export keeps its
existing destination picker and behavior.

Title Block Values groups source-file parameters first, preserving their
parameter order. A horizontal divider separates title-block-only values below.
The settings and dialog labels are translated into Czech, English, German,
French and Russian.

The Assembly Insert Component icon has an unfilled top, yellow left face and
Part-blue right face. The ordinary Insert Component action accepts a native
Skeleton Part and places it in the existing Skeleton tree slot after Origin;
the existing single-Skeleton and dependency checks still apply.
