# Bend endpoint dimension presentation

The user requested all four start/end offset dimensions outside the Bend arc
on 2026-10-19. `bend_offset_dimension_display` changes only a transient display
copy of the start or end Sketch. It places default endpoint labels on negative
local Y, away from the arc at both profile frames. Their local X positions,
measurement axes, witnesses, IDs, equations, values and solution sides stay
unchanged. In particular, signed zero is not normalized or reversed.

The helper recognizes the four Bend endpoint dimension IDs and only their
standard 8 mm witness offset. Other authored placements are retained. It is
consumed by ordinary Sketch display, embedded Bend parameter display and the
Bend preview. No native document schema or shared placement contract changes.
Opening or inspecting dimensions does not persist the display copy or create
an Undo transaction. There is no new localized application text.

The native attachment regression adds checks for all four default placements,
unchanged source serialization, signed values, solution-side identity and
preservation of custom placements across its angle/orientation cases. The final
native Bend command regression passed on Windows, including its dependent sheet
operations (`build/bend-outside-offsets-native-final.log`).

The general automatic dimension envelope initially overrode the transient
placements. Its display resolver now leaves these four Bend defaults at their
authored display positions while retaining explicit user layouts and the normal
envelope for other dimensions. The Windows GUI regression checks one occurrence
of each dimension and all four actual line coordinates after inline radius,
angle and endpoint edits, native save/reopen, Properties/Cancel and attachment
to a Flat. `build/bend-offset-isometric-gui/` records the pass and an actual
isometric screenshot for user review. The native matrix covers signed zero;
the GUI fixture uses 0/40 mm start coordinates and 3/10 mm end offsets. A GUI
case with four zero offsets and mouse double-click editing of every offset has
not been verified. Visual approval and Linux execution remain separate.
