from __future__ import annotations

from PySide6.QtWidgets import QDoubleSpinBox


class PositiveQuantitySpinBox(QDoubleSpinBox):
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
