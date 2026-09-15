# ZIMA-CAD — engineering dimensions and geometric symbols

This document provides a design foundation for dimensions in Sketcher, the model
and the developing Drawing module, based on common ISO 129, ISO GPS and ASME Y14.5
practice.

## Fundamental rule

A dimension must not be stored merely as rendered text. Keep separate:

- geometric value used by the solver;
- dimension meaning and type;
- graphical symbols;
- user prefix and suffix text;
- tolerances;
- precision and display mode.

Example:

```json
{
  "dimension_type": "diameter",
  "value": 25.0,
  "prefix_symbols": ["diameter"],
  "prefix_text": "",
  "suffix_text": "H7",
  "tolerance": null
}
```

Display:

```text
⌀25H7
```

Generated diameter labels use **⌀ (U+2300)**, matching Dimension Properties.
Shared formatting applies to Sketcher, 3D View, Drawing and exported labels.
The mm unit directly follows the number, for example `⌀25,000mm`.

## Dimension types

- Linear dimension
- Diameter
- Radius
- Spherical diameter
- Spherical radius
- Controlled radius
- Arc length
- Thickness
- Depth
- Square section
- Taper and slope
- Chamfer
- Thread
- Pitch circle
- Repetition count
- Reference and typical dimensions
- Minimum and maximum values
- Tolerance

## Symbols before the value

| Notation | Meaning | Example |
| --- | --- | --- |
| `⌀` | Diameter | `⌀20` |
| `R` | Radius | `R5` |
| `SR` | Spherical radius | `SR50` |
| `S⌀` | Spherical diameter | `S⌀100` |
| `CR` | Controlled radius | `CR12` |
| `□` | Square section | `□30` |
| `⌴` | Counterbore | `⌀10⌴⌀18` |
| `⌵` | Countersink | `⌀6⌵90°` |
| `↧` | Depth | `↧15` |
| `⌒` | Arc length | `⌒50` |
| `t` | Thickness | `t3` |
| `M` | Metric thread | `M10` |
| `G` | BSPP pipe thread | `G1/2` |
| `Rp` | Internal parallel pipe thread | `Rp1/2` |
| `Rc` | Internal tapered pipe thread | `Rc1/2` |
| `R` | External tapered pipe thread | `R1/2` |
| `Tr` | Trapezoidal thread | `Tr40×7` |

Further thread notation includes `UNC`, `UNF`, `ACME` and left-hand designation `LH`.

## Symbols and text after the value

| Notation | Meaning | Example |
| --- | --- | --- |
| `±` | Symmetric tolerance | `20±0,1` |
| `MAX` | Maximum value | `R0,5MAX` |
| `MIN` | Minimum value | `3MIN` |
| `REF` | Reference dimension | `35REF` |
| `TYP` | Typical dimension | `R5TYP` |
| `4X` | Repetition count | `4X⌀8` |
| `6 PLCS` | Number of places | `⌀5 6 PLCS` |
| `THRU` | Through all material | `⌀10THRU` |
| `EQ SP` | Equally spaced | `8X EQ SP` |
| `AF` | Across flats | `17AF` |

`TYP`, `REF`, `MAX`, `MIN`, `THRU`, and `H7` remain ordinary strings; they do not
require graphical symbols.

## Limit deviations

The dimension editor must support three tolerance representations.

### Symmetric tolerance

One value with `±` (U+00B1) appears on the same line after nominal size:

```text
20±0,010
```

### Single deviation

One signed positive or negative deviation can follow nominal size:

```text
20+0,020
20−0,010
```

Its entered sign is part of the data and must not be changed automatically.

### Upper and lower deviations

An asymmetric tolerance is neither stored nor displayed as slash-separated text.
It comprises separate upper and lower deviations. The upper is drawn above the
lower, to the right of the nominal value, both in smaller type:

```text
      +0,020
20
      −0,010
```

Both deviations may be nonnegative, or the upper deviation may be zero:

```text
      +0,100           +0,000
50                 30
      +0,000           −0,050
```

Sign must be preserved even for zero. An absolute number alone is insufficient;
the model must retain sign and upper/lower role, for example:

```json
{
  "tolerance_mode": "deviations",
  "upper_deviation": "+0.020",
  "lower_deviation": "-0.010"
}
```

Modes are `symmetric`, `single_deviation` and `deviations`; an empty mode means
no tolerance. Display decimal separators follow document settings/localization;
internal numeric representation may use a decimal point. Do not trim deviation
precision automatically: trailing zeroes express prescribed precision.

## Chamfers, tapers and pitches

Examples:

```text
2×45°
C2
1×30°
1:10
1:50
5%
⌀100PCD
6X⌀8EQ SP
```

## Threads

Examples:

```text
M10×1,5
M12-6H
M16×2-6g
G1/2
Tr40×7
M8×1LH
1/4-20UNC
1/4-28UNF
1"-5ACME
```

## Initial graphical CAD symbols

Prepare these as custom vector symbols; Unicode is a text fallback only:

| Symbol | Unicode | Meaning |
| --- | --- | --- |
| `⌀` | U+2300 | Diameter |
| `□` | U+25A1 | Square |
| `⌴` | U+2334 | Counterbore |
| `⌵` | U+2335 | Countersink |
| `↧` | U+21A7 | Depth |
| `⌒` | U+2312 | Arc length |
| `∠` | U+2220 | Angle |
| `°` | U+00B0 | Degrees |
| `±` | U+00B1 | Plus/minus |
| `×` | U+00D7 | Multiplication |
| `≈` | U+2248 | Approximately |

Do not substitute `∅` (U+2205, empty set) for engineering diameter `⌀`.
The first Dimension Properties editor offers this palette for prefix/suffix text,
inserting the selected symbol at the cursor. It deliberately omits `∅`.

## GPS — geometric tolerances

Custom vector rendering is appropriate for straightness, flatness, circularity,
cylindricity, line profile, surface profile, parallelism, perpendicularity,
angularity, position, coaxiality, symmetry, circular runout and total runout.
Unicode is unavailable or typographically unreliable for some GPS symbols.

## Surface texture and welds

A separate vector library will also be needed for basic surface-texture symbols,
surfaces requiring/prohibiting material removal, fillet/V/X/U/J welds, spot/seam
welds, all-around welds and field welds. Welding support will follow the developing
Drawing module's requirements and ISO 2553.

## ZIMA-CAD display rules

- Passive dimensions omit length units.
- Passive dimensions hide trailing zeroes.
- Editing shows the exact numeric value.
- Prefix, value and suffix concatenate without automatic spaces.
- Text starts after the leader and grows left to right.
- Symbols need visually consistent size, stroke and baseline.
- Critical geometric symbols use vectors rather than arbitrary system-font glyphs.
- Units remain saved and used for calculations/conversions even when passive
  dimensions omit them.
