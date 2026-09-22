# Document naming policy

The global `config/config.ini` can enable three independent input conversions:

```ini
[DocumentNames]
Uppercase=true
RemoveDiacritics=true
ReplaceSpaces=true
```

A missing key or `false` disables its conversion. With all keys absent, names
retain the previous behavior. The checked-in configuration enables all three.
The GUI exposes these options under Global Settings > File names. Confirming
OK applies them to subsequent operations without a restart. Cancel preserves
the previous values. Restart after manually editing the configuration file.
New Document starts with an empty filename field and converts user input while
typing according to the active options.

For example, `Příruba čelní` becomes `PRIRUBA_CELNI`. Unicode combining accents
are removed, uppercase conversion is Unicode-aware, leading/trailing whitespace
is removed and each internal whitespace run becomes one underscore. These
operations do not translate words. Native extensions stay lowercase.

The GUI and command host share the policy for New, Rename File and Save As.
Only the submitted filename is converted; parent directories, existing source
paths, document contents, notes and title-block values are not rewritten.
Open and ordinary Save do not rename existing files. Collisions are checked
after conversion and cannot overwrite another native document. Explicit
case-only renaming is supported on Windows through the existing staged native
rename transaction, including dependency updates.

Factory start templates are `START_PART.prtz`, `START_ASSEMBLY.asmz` and
`START_SKELETON.prtz`. Their file paths are configuration references, not input
to the naming conversion. Skeleton detection remains case-insensitive.
User-defined template filenames are respected as configured.

Verification covers independent and combined settings, composed/decomposed
Unicode, absent settings, GUI/CLI configuration parity, creation from factory
templates, rename, Save As, normalized-name collisions and case-only renaming.
