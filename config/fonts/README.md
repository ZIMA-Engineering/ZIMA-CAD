# Drawing fonts

This directory contains fonts bundled with ZIMA-CAD for technical drawings.
Drawing fonts must not depend on fonts installed in the operating system,
because substitutions change text metrics and can break title blocks,
dimensions, and other precisely positioned drawing content.

The default font expected by `config/drawing.ini` is:

```text
osifont-lgpl3fe.ttf
```

The font binary is bundled together with `OSIFONT-NOTICE.txt` and the complete
LGPL version 3 text in `LICENSE-LGPL-3.txt`.
Do not add AutoCAD `ISOCP` or `ISOCPEUR` font files without explicit permission
to redistribute them.


Native Sketch text embeds this same OSIFONT file into `zima_sketcher` at CMake
configuration time. Both the Properties dialog and command-line modeling use
FreeType for outlines and HarfBuzz for glyph shaping; neither substitutes a
system font. The repository's existing font notice and license remain required
with the font. `cpp/vcpkg.json` explicitly declares the two libraries, which
were already present in the Qt/OCCT development dependency tree.

FreeType is available under the FreeType License or GNU GPL, as recorded in its
package copyright file. HarfBuzz records the Old MIT license. Keep the dependency
licenses with any future packaged runtime under the release packaging rules;
this local development change does not create or publish a release archive.
