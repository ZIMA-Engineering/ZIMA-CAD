# Dimension witness overrun

The user confirmed a fixed 1.5 mm witness-line extension on 2026-09-29.
It continues beyond the dimension line at the arrow tip, away from the measured
geometry. The arrow and its grip stay at the dimension-line intersection.

The shared projected presentation applies this to linear and angular witnesses.
Drawing annotation layouts use paper millimetres, drawing measurement rendering
uses 1.5 times the paper zoom, and the 3D View uses 1.5 times logical DPI / 25.4.
The 3D overrun therefore stays independent of model zoom. Running dimensions
use the same paper length. Radius leaders and angular leader-only presentations
are not witness lines and retain their current presentation.

Breaks and jogs keep their geometry-to-arrow parameterization; the overrun is
appended after the edited witness. No model coordinates, measured values,
reference identities or persisted dimension layouts are changed.

ISO 129-1:2018, clause 5.5, describes an overrun relative to thin-line width.
The fixed 1.5 mm length is the user's explicit product setting rather than a
claim of compliance for every configurable line width.

Reference: [ISO 129-1:2018](https://www.iso.org/standard/64007.html).
