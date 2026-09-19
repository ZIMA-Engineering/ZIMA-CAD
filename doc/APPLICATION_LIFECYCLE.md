# Application language and closing documents

## Language changes

Global Settings stores the selected application language and offers to reopen
the application interface. A complete workspace window is recreated so menus,
toolbars, Tree controls, Drawing controls and later property dialogs use the
same language. Changing the language does not regenerate model geometry.

Before reopening, the same unsaved-document guard used for ordinary application
exit offers Save All, Discard and Cancel. Saved native documents are reopened
afterwards; discarded new documents without a saved file are not reopened.
The configuration directory that supplied the language is retained even if
saving a document changed the working directory.

An update installation already owns process shutdown/restart; applying its
Settings must not start a competing language restart.

Declining the restart, cancelling document confirmation or failing to save
keeps the current window open in its original language. Both translation maps
and the active Qt translator stay consistent until the next complete startup.
The selected language remains saved for that startup. Reopening Settings shows
the pending saved language and allows retrying or changing it back.

## Application exit

The main multi-document workspace checks all open documents, including loaded
source documents outside the currently displayed tab. Family instances share
their owner's persistence state and do not cause duplicate save prompts.
Parts, Assemblies, Drawings, title blocks and drawing formats use their existing
save implementations and native formats.

One confirmation lists every document requiring a save. Cancel is the default.
Save All proceeds only if every requested save succeeds. A cancelled file dialog
or write failure keeps the window and documents open. Documents successfully
saved before a later failure remain saved. Discard explicitly authorizes closing
without writing the pending changes.

An open editing dialog must first be completed or cancelled. Closing the main
window raises that dialog without committing or discarding it. Ordinary active
Sketch editing and inline dimension editing also block shutdown until finished.
The permanent title-block/format Sketch workspace remains saveable on exit.

## Verification

The application lifecycle GUI contract exercises Save All, Discard, Cancel,
write failure, pending dialog protection, all five UI languages, deferred and
cancelled restarts, saved-file reopening and repeated application event loops.
Release acceptance records the actual executed checks separately.

On September 19, 2026, the new GUI contract passed together with six existing
contracts: portable settings, instance startup/reservations, native document
operations, native Family Table, Updates UI and Family Table UI. Logs are
`build/lifecycle-ui-tests.log` and `build/lifecycle-regressions.log`.
