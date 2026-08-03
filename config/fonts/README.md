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
