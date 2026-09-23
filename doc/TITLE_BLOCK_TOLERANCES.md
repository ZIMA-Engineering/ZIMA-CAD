# Factory title-block tolerance fields

The factory title blocks store general tolerancing references as local Drawing
fields rather than model parameters. Their list/custom-value editor uses the
existing `TextAction` contract. Changing a field never writes a parameter to
the Part or Assembly. All five locales use the same standard identifiers.

The default dimensional tolerance is `ISO 2768-m`, as explicitly agreed with
the user on 2026-09-23. The Accuracy list contains exactly `ISO 2768-f`,
`ISO 2768-m`, `ISO 2768-c` and `ISO 2768-v`. The GPS principles field remains
`ISO 8015:2011`. Both fields are local to the title block; custom text is allowed.

The factory list does not offer H/K/L combinations or ISO 22081. ISO 8015
defines GPS principles, not a general geometrical tolerance value. Any required
geometrical tolerances must therefore be prescribed separately on the drawing.
ISO 2768-1:1989 remained published when checked on 2026-09-23;
the next ISO 2768 edition was listed as under publication. An unpublished draft
is not used as a factory requirement. Selection does not automatically translate
old classes into an ISO 22081 tolerance.

Sources checked on 2026-09-23:

- [ISO 22081:2021 and its replacement history](https://www.iso.org/standard/72514.html).
- [ISO 2768-2:1989 withdrawal](https://www.iso.org/standard/7749.html).
- [ISO 2768-1:1989 status](https://www.iso.org/standard/7748.html).
- [Next ISO 2768 edition status](https://www.iso.org/standard/85741.html).
- [ISO 8015:2011](https://committee.iso.org/standard/55979.html?browse=tc).
- [ISO 2768-2 preview, class tables](https://cdn.standards.iteh.ai/sist-preview/7749/116a03deb4a249189e32884ce312bd29/ISO-2768-2-1989.pdf).

Factory regeneration uses the native symbol catalog and projection generators,
then `zima_cpp_symbol_integration_tests --update-factory`. The update refreshes
embedded definitions and editable title fields and saves the native start
templates. Existing user Drawing files retain their embedded title blocks.
