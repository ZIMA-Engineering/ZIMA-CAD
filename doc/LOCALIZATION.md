# User-interface localization

Global Settings supports Czech (`cs`), English (`en`), German (`de`), French
(`fr`) and Russian (`ru`). Confirming a language change offers a full application
window restart. Saved documents reopen; cancelling restart keeps the current
window language and retains the selected language for the next startup.
The ISO application font remains unchanged across language changes.

## Catalogs and resource paths

Every language has two UTF-8 catalogs under `config/localization`:

- `<language>.ini`: `[Translations]` contains named `ApplicationSettings::text`
  keys; `[QtTranslations]` contains source-text translations.
- `<language>.qt.json`: additional source-text translations, loaded after INI.
  JSON preserves multiline strings, leading/trailing spaces and equals signs
  in keys. Both the GUI and native CLI load these catalogs.

Qt uses the shared source entry or a more specific `Context|Source` entry.
Replacing the translator retires the previous translator. Unknown messages
fall back to their source; this is not a substitute for completing all five
catalogs. Numerus messages require a separate plural-aware implementation.

Paths remain relative to the configuration file that owns them. When saving
an inherited path into a working-directory configuration, rebase it against
that writable configuration. Otherwise changing language can accidentally
redirect localization, templates and materials to nonexistent local folders.
Installed portable configuration retains its layered ownership rules.

## Names and document content

New feature defaults follow the selected application language, including
primitives, Extrusion/Revolution, Sketches, construction objects, sweeps,
patterns, mirrors and sheet-metal features. New Bodies and drawing sheets use
localized default names. Creation uses translation before the property dialog
opens; edits retain the stored name. Explicit names supplied by users or command
arguments are never translated. Imported source names are retained.

Changing application language does not rewrite existing document names,
parameters, metadata, object names or title-block labels. Persisted Origin
identity and reference keys are not localized. Title-block templates and their
value locale are selected independently of the application language.

## Requirements for every change

The binding rule is recorded in [AGENTS.md](../AGENTS.md). New or changed visible
text must be translated into all five supported languages in the same change.
Use `tr()` for static text and `QT_TR_NOOP` for source literals translated later.
Use the named catalog for existing settings-based UI. Translate errors at their
presentation boundary, without changing internal error codes.

Preserve placeholders (`%1`, `%2`), parameter tokens (`&bom.quantity`), file
extensions and internal identifiers. Keep palette category identity separate
from its translated display name. Prefer JSON for new source-text messages.
Do not translate user-authored palette or object names.

## Verification

`zima_cpp_translations_contract` loads all real catalogs, verifies matching
source and named key sets, checks placeholders, contexts and translator
replacement, and scans production `tr()`/`QT_TR_NOOP` literals for omissions.
Actual Qt buttons, lock tooltips and property dialogs are exercised in all five
languages. The test also checks Russian alphabet coverage, including both Yo
characters, in the bundled `osifont-lgpl3fe.ttf`.

`zima_cpp_application_lifecycle_ui_contract` exercises restart through Global
Settings for all five languages, reopening documents and checking the font,
main menu, standard-view selector, Tree section folder and new feature names.
This covers inherited configuration paths as well as catalog lookup.

`zima_cpp_part_dialog_layout_contract` opens Part feature dialogs in all five
languages at 1366×768 and 1920×1080. It checks window containment, editor bounds,
table-cell controls and confirmation buttons, including populated 2D Sweep
stations. The 2D Sweep path-plane row reserves 32 px for the shared 30 px
reference controls; smaller columns allowed the arrow and inspection button to
overlap adjacent cells. Mirror and Pattern reference tables use the same minimum;
Pattern count and distribution columns size themselves to translated content. The test includes primitive, profile, construction,
sweep, section, copy, Body/Boolean and sheet-metal property dialogs.

The source scanner is a regression guard, not a natural-language proof: review
dynamic text, user-content boundaries and visible layout when adding UI.

## Localized company title blocks

`config/formats/ZE-TITLE-BLOCK-CS.tblz` is the Czech source template. Language variants
are `ZE-TITLE-BLOCK-{CS,EN,DE,FR,RU}.tblz`. Each embeds the company SVG logo from
`config/formats/ZIMA-Engineering.svg`, with its aspect ratio preserved, and
retains the original geometry, constraints, data fields and BOM tokens.

To regenerate variants after updating the Czech source, run:

```powershell
python tools/localize-title-block.py --cli build/cpp-windows-release/zima-cad-cli.exe
```

The generator uses native `template.sketch.edit` and `template.save` commands
to rebuild translated font outlines. It checks unchanged geometry and field
tokens. Review native drawing exports for label fit after changing translations.
Language variants can be opened without access to the original external logo.

## Git and release resources

Catalogs, font, templates, title blocks, SVG and material library are tracked
project resources. Windows `tools/distribution/package.py` and Linux
`tools/distribution/build-linux.py` recursively copy committed `config` and
`resources` into the versioned runtime. Numeric backup files are excluded from
runtime resources. Shared user configuration remains outside immutable versions.
Changes become part of a release when that committed revision is packaged;
updating a local development build does not publish a new release archive.

Material directories use English names (`01_steels`, `02_nonferrous_metals`,
`03_cast_irons`, `04_plastics`, `05_glass_ceramics`, `06_construction`) and English
subcategory names. Existing material filenames and contents are retained;
user-removed library entries are not restored.
