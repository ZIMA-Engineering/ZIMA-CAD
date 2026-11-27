# Website and shared visual guides

The repository-root `www/` builds the public website and the identical offline
guide pages. The first set contains 11 authored guides, 44 numbered annotations,
three authentic CAD screenshots and one short FEM deformation video in the
gallery. The original project logo is reused. Website interface and marketing
copy cover Czech, English, German, French and Russian; guide prose remains English
under the project documentation policy.

## Verified scope

- `python www/check.py`: 76 HTML files, 1,581 local references, complete language
  catalogs and alternates, and the exact 113-file runtime/public inventory.
- Staged Git blob hashes match every public manifest entry. Public text uses LF
  across Windows, Linux and committed-source packaging. Binary media is unchanged.
- Gallery discovery was exercised by adding and removing an image without editing
  a media list. The local preview rebuilds and refreshes open pages after source
  changes; static hosting requires a new generated upload.
- Browser inspection covered desktop and 390-pixel mobile layouts, all five home
  languages, dark/light themes, guide language switching, accent-insensitive search
  and its empty state, the image dialog, numbered annotation selection and portrait
  screenshots. No horizontal overflow was observed at the checked sizes.
- The three-second H.264 FEM video loaded and played. Hidden slideshow items stop
  playback; reduced-motion preferences disable automatic advancement. Initial
  slideshow selection varies between visits when browser storage is available.
- Windows native GUI and browser targets built with the existing Qt 6.11 and
  OCCT 8.0.1 SDK. The repository-root `zima-cad.bat` launcher is unchanged.

The following Windows CTest contracts passed:

| Contract | Coverage relevant to this change |
| --- | --- |
| `zima_cpp_documentation_ui_contract` | Actual toolbar context-menu event, localized action, new guide tab, loaded images and annotations, closing back to an unchanged saved Part, Help index, File Manager Overview using the same source, and active editor guard in all five languages |
| `zima_browser_ui` | Existing browser navigation, bookmarks, downloads, themes and credentials; scoped offline guide page and stylesheet; rejection of out-of-root navigation; external links opening an ordinary web tab without local-file access |
| `zima_cpp_browser_workspace_ui_contract` | Native browser/workspace tab lifecycle |
| `zima_cpp_file_manager_workspace_ui_contract` | Existing File Manager/workspace behavior |
| `zima_cpp_portable_settings` | Factory guide source follows the current runtime while preserving custom settings |
| `zima_file_manager_portable_sources_ui` | Portable source UI |
| `zima_file_manager_source_branding_ui` | Existing source presentation |
| `zima_cpp_translations_contract` | Localization coverage and catalogs |

## Boundaries

This is an initial guide set, not documentation for every command. Unmapped
commands have a disabled contextual guide action. Screenshots of pending dialogs
are identified as such; they do not claim that the displayed definition has
calculated successfully. Modeling algorithms, native document formats and the
protected placement solver were not changed.

Windows and Linux packaging share the same public-site copy helper. Exact copied
file inventory and hashes were verified locally; a Linux native build and GUI run
were not performed. No new versioned portable release was produced by this task.

Deployment uses only the public allowlist. Connection data, private browser
profiles, local configuration, source tooling and backup files are excluded.
