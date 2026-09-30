# Directory chooser label persistence

The working-directory chooser now supplies its translated acceptance label
through `QFileDialog::setLabelText(Accept, ...)`. Setting the child button text
directly was temporary: entering another directory made Qt restore `&Choose`.
The chooser reuses the existing `button.select` translation in Czech, English,
German, French and Russian. Directory selection and cancellation are unchanged.

The translation contract opens the actual application chooser in each language,
enters a nested directory, returns to its parent and verifies the acceptance
label and Cancel result. It processes the language-change events before opening
the modal dialog, matching ordinary application use. The test fails against the
previous implementation with `&Choose` after both navigation steps, and passes
with the persistent label. The same contract validates catalog coverage and
formatting placeholders across all five languages.

Run `ctest --test-dir build/cpp-windows-release -R '^zima_cpp_translations_contract$' --output-on-failure`.
