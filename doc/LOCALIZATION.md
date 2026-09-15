# User-interface localization

Choose application language in **Global Settings → Application Language**. Available
languages are Czech (`cs`), English (`en`), German (`de`), French (`fr`), and Russian
(`ru`). Effective config uses `Application/Language` and `Paths/Localization`.
Working-directory `config.ini` overrides base `config/config.ini`; that choice survives
settings confirmation.

New Properties dialogs use the changed language immediately. Restart to update every
already open menu, panel, and dialog; Global Settings includes this notice. Language
changes do not translate user object/file names, title-block text, or persisted model values.
Project documentation is maintained in English; exact localized strings below are examples.

## Newly localized features

All five languages include value locks, one-time distance/angle capture, title-block
images, and BOM regions: commands, properties, alignment, repeat directions, help,
input/save errors, file filters, and shared OK/Cancel buttons.

| Czech | English | German | French |
| --- | --- | --- | --- |
| Zamknout hodnotu | Lock value | Wert sperren | Verrouiller la valeur |
| Odemknout hodnotu | Unlock value | Wert entsperren | Déverrouiller la valeur |
| Obrázek | Image | Bild | Image |
| Vlastnosti obrázku | Image Properties | Bildeigenschaften | Propriétés de l’image |
| Oblast kusovníku | BOM region | Stücklistenbereich | Zone de nomenclature |
| Zachovat poměr stran | Keep aspect ratio | Seitenverhältnis beibehalten | Conserver les proportions |
| Zrušit | Cancel | Abbrechen | Annuler |

See [Numerical value locks](NUMERIC_VALUE_LOCKS.md) and [Drawings](DRAWINGS.md) for
lock behavior, PNG/SVG insertion, and BOM repetition.

## Editing language files

Catalogs are UTF-8 `config/localization/{cs,en,de,fr,ru}.ini`. C++ reads two sections:

- `[Translations]`: existing named keys for `ApplicationSettings::text`, such as
  `global.language`, also used by menus/file selection.
- `[QtTranslations]`: C++ `tr()` / `QObject::tr()` source text, for example
  `Zamknout hodnotu = Lock value`. Application-owned QTranslator is replaced when
  config changes, without accumulating old language translators.

Use `Context|Source text` when Qt contexts need different translations; this overrides
the shared source-text entry. Context comes from the `Q_OBJECT` class providing
`tr()`, not necessarily the derived dialog name. Unknown text falls back to source
language. This section handles nonplural text; messages with `n` need plural-aware catalogs.

Add identical keys in all five languages. Preserve `%1`, `%2`, etc., `&bom.item_number`,
`&bom.quantity`, and file-filter extensions exactly. Lines split at the first `=`,
so keys cannot contain it. Text is single-line with trimmed edges. Do not translate
internal identifiers such as `center`, `middle`, `up`, or lock keys.

## Verification

`zima_cpp_translations_contract` loads all five real catalogs through local config,
checking matching keys, placeholders, translator replacement, contexts, and source
fallback. Box Properties verifies permanent-lock help, both one-time-capture states,
and Cancel.

`ZIMA_VERIFY_TEMPLATES_ONLY=1` in `zima_cpp_workspace_startup_contract` opens actual
Image/BOM-region Properties in all five languages, checks text and preserved alignment,
and captures `Projects/test/image-properties-*.png` and `Projects/test/bom-properties-*.png`.

## Russian and Parameters

The Russian catalog contains all English keys, including parameters, materials,
units, modeling commands, and new text modes. Part/Assembly start templates include
Russian standard-parameter labels. Parameters language selection offers `ru`; shared
values are not translated. Existing documents can receive Russian labels/values in
Parameters. UI language changes neither parameter keys nor user document content.
Title-block value language is selected separately in Sheet Properties.

## Czech interface audit (2026-09-11)

Standard Qt buttons, file dialogs, and editing menus now have translations, including
mnemonic `&` variants. Unsaved-document confirmation uses **Uložit / Neukládat / Zrušit**
(Save / Don't Save / Cancel). Translating only Save As menu text is insufficient:
QMessageBox/QFileDialog buttons look up their own Qt source strings.

Newer Czech labels include **Odsazení**, **Obrátit**, **Skica**, **Tenkostěnný**,
**Skořepina**, **Booleovská operace**, and **Meze vazby**. Show/Erase now translates
both buttons through `tr()` instead of fixed SHOW/ERASE. New keys exist in all five
languages. This audit does not certify complete translation of every older Czech
source message into other languages or all kernel diagnostics.

`zima_cpp_translations_contract` additionally creates an actual unsaved-document
QMessageBox and non-native save QFileDialog in each language, checking button text,
Offset Properties, and Flip names. It saves Czech confirmation as
`unsaved-document-cs.png` in the test working directory.

The red document-tab close button uses shared `TabCloseButton`, drawing a centered
white cross from two segments independently of font metrics and retaining 10 px
right-slot inset. The existing offset test captures `build/tab-close-centered.png`.
Verification includes Windows Release build, translation/offset/Show-Erase tests,
and visual screenshot inspection.
