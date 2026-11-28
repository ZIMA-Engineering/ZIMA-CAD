# ZIMA-CAD website and shared visual guides

The repository-root `www/` contains the website for `https://zima-cad.org` and
the same generated guide pages opened by the native CAD application. This is
separate from the private browser profile under `config/www/`.

## Structure

| Location | Purpose |
| --- | --- |
| `0000-index/metadata.ini` | Website identity, public social links and gallery settings |
| `0000-index/index_<language>.html` | Generated home pages |
| `0200-gallery/images/` | Authored screenshots and short MP4/WebM recordings |
| `0200-gallery/0000-index/` | Generated gallery pages |
| `0300-documentation/0000-index/metadata.ini` | Shared guide registry and stable command IDs |
| `0300-documentation/<guide>/0000-index/` | Generated individual guides |
| `0400-download/0000-index/` | Release download entry pages |
| `_source/` | Shared stylesheet, browser behavior, translations and output inventory |
| `../doc/guides/` | The only authored guide text, images and annotation coordinates |
| `../doc/guides/commands-*.json` | Localized procedures and source evidence for individual native commands |

The five website languages come from `config/localization/metadata.ini`.
Navigation, labels and marketing copy are translated in `_source/translations.json`.
The user guides, screenshot explanations and command procedures are maintained
in Czech, English, German, French and Russian under the user's explicit guide
localization requirement. Each page uses native screenshots captured in its own
language. The build rejects missing translations or screenshots instead of
substituting another language.

## Build and preview

Python is a build tool, not a product runtime dependency.

```powershell
python -m pip install --target build/www-tools -r www/requirements.txt
python www/build.py
python www/check.py
python www/preview.py
```

The preview listens only on `127.0.0.1:8765`. It watches authored sources and
refreshes open pages after a successful rebuild. It serves only the public output
inventory, not arbitrary repository files. Stop it with Ctrl+C.

Add PNG, JPEG, WebP, MP4 or WebM files to `0200-gallery/images/`. No handwritten
image list is needed. The preview rebuilds automatically; a public static host
needs a new build and upload. Filenames supply fallback captions, so use
descriptive names. Map authored media to translation keys in `[GalleryCaptions]`
to show a localized title below every preview in both the home slideshow and
the gallery. The current five media files have captions in all five languages.
`sheet-metal-from-solid.mp4` is the complete user-supplied recording, delivered
at 1920 × 1044 with H.264 video, AAC audio and MP4 fast-start metadata.
Videos use native controls, muted inline playback in the home slideshow and
manual playback in the gallery. Hidden slides stop video playback. Reduced-motion
preferences disable automatic slideshow playback. The first item varies between
visits when browser storage is available.

Use short, web-compatible recordings. H.264 MP4 and WebM are appropriate delivery
formats; avoid uploading large raw recordings. Original screenshots are retained
unchanged. Annotated guides render numbered arrows over the original image using
percentage coordinates, so the complete image must keep its aspect ratio.

## Add a guide

1. Write the English overview in `doc/guides/` and its four translations in the
   corresponding `cs/`, `de/`, `fr/` and `ru/` directories. Add its stable slug
   and English source path to `[Guides]` in the shared documentation metadata.
2. Add the displayed title to all five translation catalogs.
3. Put authentic screenshots in `doc/guides/images/<language>/`. Add their common
   filename, English alt text and marker positions to `doc/guides/annotations.json`,
   and translated alt text and markers to each locale's `annotations.json`.
   A localized marker may override its coordinates for that capture.
4. Keep overview guides separate from the action-specific command registry below.
5. Rebuild, check links and inspect every marker at desktop and mobile widths.

The screenshots explain the state actually captured. A partially filled dialog
is not evidence of a completed operation. Guide steps and documented limits must
continue to agree with the current native implementation.

## Command guides

`[CommandCatalogs]` names the three authored catalogs: modeling, sketch/FEM and
workspaces. Together they currently describe all 145 right-toolbar actions.
Each exact action ID has a unique `command-*` slug, a workspace `group`, verified
repository `sources`, and `locales` containing exactly `cs`, `en`, `de`, `fr`,
and `ru`. Every locale provides nonempty `title`, `intro`, `prerequisites`,
`result`, `note`, and three to five `steps`. Titles use the current localized
native action label. Source evidence is validated locally and is not published
as extra repository files.

Every command requires an actual native button capture at
`doc/guides/images/commands/<language>/<action>.png`. An optional `dialogCapture`
names an existing native capture case, requiring
`doc/guides/images/dialogs/<language>/<case>.png` in all five languages. The build
copies those PNG bytes unchanged into the public documentation image tree.
It generates five pages and localized directory metadata per command, alongside
the existing eleven overview guides. The index groups commands by workspace;
localized search matches titles, procedures and stable action IDs. Navigation
and grouped links also work without JavaScript.

`[Commands]` must map every catalog action to its exact slug. The older generic
`sketchAction`, `extrusionAction` and `revolutionAction` aliases map to the
corresponding current Feature shortcuts. Unknown actions, duplicate slugs,
incomplete text and missing images stop generation before output is written.
The generated public-file manifest automatically includes every command page,
localized metadata file and required image for both hosting and native packages.

`check.py` verifies localized `.guide-copy` elements, exact command-to-page
identity, matching-language native image paths and unchanged image bytes, in
addition to links, alternate-language routes and runtime packaging.

## Native application

The factory File Manager source `documentation` points to
`../www/0300-documentation` relative to the factory `config/` directory. Change
its location through the existing **Global settings > File Manager** source
table. Both the File Manager and contextual help use that same location.

The File Manager's Overview tab displays the localized HTML index. Right-clicking
a command button offers **Show guide** when its stable ID has an available guide.
The Help menu opens the guide index. Both use an ordinary new browser tab and
preserve the existing active-editor guard. Missing mappings are disabled rather
than opening a misleading guide. The command registry covers the current right
toolbar; menus and context actions outside that inventory may have separate
documentation or share an existing procedure.

Offline guide tabs can read assets only within the configured website directory.
External links open an ordinary web tab; ordinary browsing retains its existing
local-file restrictions. The guide reader does not require a local HTTP server.

Windows and Linux packaging consume the checked public output inventory, including
offline guides, styles, artwork and media. They do not copy the user's browser
profile. Native Windows integration and Linux execution are separate verification
boundaries.

## Static deployment

```powershell
python www/build.py --check
python www/check.py
python www/build.py --stage dist/website-ready
```

The staging directory must be empty. Upload only its contents, not the repository,
authored sources or local build tooling. The public manifest is an explicit
allowlist and verifies generated-file hashes before runtime packaging. Server
credentials are not part of the website, its metadata or deployment artifacts.

Back up and verify the existing remote website before replacing it. Verify the
published home page, guide pages and media before retiring the old files.
