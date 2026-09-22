# Symbol library

Library structure for reusable symbols:

- `surface-texture`: surface texture and roughness indications.
- `welding`: welding symbols.
- `geometric-tolerances`: form, orientation, location and run-out indications.
- `machine-elements`: machine-element symbols and reusable representations.
- `drawing-conventions`: projection methods and other drawing conventions.
- `general`: other symbols without a more specific category.

The first asset is `drawing-conventions/ZE-PROJECTION-METHOD.symz`, containing
first-angle and third-angle variants in one native Sketch. The C++ library can
read, validate and save it. Other categories remain placeholders. GUI insertion,
editing, text parameters and live Drawing-setting bindings remain to be implemented.
See `doc/SYMBOLS_DESIGN.md` in the repository for the format and current scope.
