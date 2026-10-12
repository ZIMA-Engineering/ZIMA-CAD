# Engineering tables

This factory library is a filtered snapshot of the ZIMA-Engineering mechanical
tables. It contains 1,448 HTML pages, styles, scripts, reference documents, data
files and preview images in 312 directories. The import intentionally excludes
legacy Pro/ENGINEER CAD files, editor backups and desktop metadata.

Every visible directory has File Manager metadata version 3. Physical directory
names are short English identifiers that keep Windows release paths within the
distribution limit. Human-readable localized names are stored in
`0000-index/metadata.ini` and shown by ZIMA-CAD. The root and main groups have
Czech, English, German, French and Russian names throughout all 312 directories.
The reviewed naming catalog is `tools/data/engineering-table-directories.json`.
It uses engineering terminology such as parallel/Woodruff keys, thread
undercuts, clevis pins and retaining rings. Short storage identifiers use whole
words rather than truncated label prefixes. Standard numbers, size ranges,
directory identities, removal protection and existing parameter sections are
preserved. Labels remain editable through Directory Properties.

HTML navigation is preserved. Shared web assets are copied beside each page and
links are rewritten to the imported English directory names, so a table does not
depend on the original source tree. The localized `index_<language>.html` page
has priority, followed by `index.html`; directories without either use the
automatic File Manager overview.

`0000-index/import.json` records the reviewed import scope. The source snapshot
was not modified. Technical review of the individual tables is a separate task;
this import does not certify their engineering content.
