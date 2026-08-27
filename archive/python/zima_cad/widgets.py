from __future__ import annotations

from PySide6.QtWidgets import QDoubleSpinBox


class PrecisionDoubleSpinBox(QDoubleSpinBox):
    """Show rounded text while retaining the full model value.

    ``QDoubleSpinBox.setDecimals`` normally rounds its stored value.  In CAD
    that makes opening and confirming an otherwise unchanged property dialog
    destructive.  This editor keeps enough fractional digits for binary64
    calculations internally and applies the requested decimal count only in
    ``textFromValue``.
    """

    # Qt permits up to 323 fractional decimal places.  Using that limit keeps
    # even very small finite binary64 values intact; 15 or 17 *fractional*
    # places would still round values close to zero before the dialog opens.
    _INTERNAL_DECIMALS = 323
    _MAX_DISPLAY_DECIMALS = 12

    def __init__(self, *args, **kwargs) -> None:
        self._display_decimals = 3
        super().__init__(*args, **kwargs)
        QDoubleSpinBox.setDecimals(self, self._INTERNAL_DECIMALS)

    def setDecimals(self, decimals: int) -> None:  # noqa: N802 - Qt API
        self._display_decimals = max(
            0,
            min(self._MAX_DISPLAY_DECIMALS, int(decimals)),
        )
        # Calling the base implementation with the UI precision would round
        # the model value.  Keep its numeric range at calculation precision.
        QDoubleSpinBox.setDecimals(self, self._INTERNAL_DECIMALS)
        self.update()

    def displayDecimals(self) -> int:  # noqa: N802 - Qt-style API
        return self._display_decimals

    def textFromValue(self, value: float) -> str:  # noqa: N802 - Qt API
        return self.locale().toString(
            float(value),
            "f",
            self._display_decimals,
        )

    def valueFromText(self, text: str) -> float:  # noqa: N802 - Qt API
        # Focus changes and dialog submission can ask the spinbox to
        # reinterpret its already rounded display string.  If the user did
        # not edit that string, preserve the exact value currently held.
        line_edit = self.lineEdit()
        displayed = str(text).strip()
        prefix = self.prefix().strip()
        suffix = self.suffix().strip()
        if prefix and displayed.startswith(prefix):
            displayed = displayed[len(prefix):].strip()
        if suffix and displayed.endswith(suffix):
            displayed = displayed[:-len(suffix)].strip()
        if (
            not line_edit.isModified()
            and displayed == self.textFromValue(self.value()).strip()
        ):
            return self.value()
        return QDoubleSpinBox.valueFromText(self, text)


class NoWheelDoubleSpinBox(PrecisionDoubleSpinBox):
    """Spinbox that ignores mouse-wheel input.

    Rotation fields sit close together and a stray scroll while the mouse
    passes over them silently changes the value.  Disabling the wheel here
    keeps typing/arrow-button editing intact while removing that hazard.
    """

    def wheelEvent(self, event) -> None:  # noqa: N802 - Qt virtual method
        event.ignore()


class PositiveQuantitySpinBox(PrecisionDoubleSpinBox):
    """Positive quantity editor with practical arrow-button increments.

    Values typed by the user retain the widget's configured precision and
    minimum.  Arrow buttons use tenths below one and whole units from one up,
    and never drive an already practical value below 0.1.
    """

    _BUTTON_FLOOR = 0.1
    _FINE_STEP = 0.1
    _COARSE_STEP = 1.0

    def stepBy(self, steps: int) -> None:  # noqa: N802 - Qt virtual method
        if steps == 0:
            return

        value = self.value()
        direction = 1 if steps > 0 else -1
        for _ in range(abs(steps)):
            if direction > 0:
                if value < self._BUTTON_FLOOR:
                    value = self._BUTTON_FLOOR
                elif value < 1.0:
                    value = min(1.0, value + self._FINE_STEP)
                else:
                    value += self._COARSE_STEP
            elif value <= self._BUTTON_FLOOR:
                # Preserve manually entered sub-0.1 values.  The arrow
                # buttons must not manufacture still smaller values.
                break
            elif value <= 1.0:
                value = max(
                    self._BUTTON_FLOOR, value - self._FINE_STEP
                )
            else:
                value = max(
                    self._BUTTON_FLOOR, value - self._COARSE_STEP
                )

        self.setValue(value)
