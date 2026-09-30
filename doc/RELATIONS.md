# Document relations

Relations are an editable UTF-8 program stored as plain source text inside the
native Part or Assembly document. Comments and blank lines are retained. Import
and export use optional UTF-8 `.txt` files; a saved model has no dependency on
those files.

## Editing and regeneration

The Relations window has a multiline editor with line numbers, syntax colours,
a dimension catalogue and an arrow for selecting dimensions in the View. Arm
the arrow, select a feature to display its dimensions, then select a dimension.
Its identifier is inserted at the remembered cursor position and its current
value is shown. The catalogue also supports double-click insertion.

OK validates and stores the source. Cancel discards pending source edits.
Regenerate evaluates the program and publishes the resulting dimensions,
parameters and Part colour together. Material changes, parameter edits, opening,
saving and display refresh do not evaluate relations. Physical properties remain
available as current calculation inputs; their corresponding user parameters
retain their last regenerated values.

A failed regeneration does not publish partially updated relation values.
Successful changes can be undone together. Dimension values are assigned before
body calculation; output parameters and colour are assigned after it.

## Syntax

Write one assignment per line. Spaces do not separate commands. Identifiers are
case-sensitive ASCII letters, digits and underscores and cannot start with a
digit. `d` followed by a number denotes a document dimension.

```text
# Numeric dimensions use the document's units; do not append mm.
d6 = 10
d3 = d1 + d2
d4 = d1 * sind(30)

# Quoted text is literal; & concatenates text and formatted values.
stock = "⌀" & d1 & "x" & d2

if d1 > 100
    d3 = 5
elseif d1 > 50
    d3 = 3
else
    d3 = 2
endif
```

The examples are independent: do not assign the same target both unconditionally
and in a conditional block. Repeated assignments are permitted only in mutually
exclusive branches of the same conditional chain. If no branch assigns a
target, its previous value remains in use. A newly introduced parameter without
an active assignment has no previous value and cannot be read.

Quoted strings can contain Unicode, spaces, punctuation and formula-like text.
Supported escapes are `\"`, `\\`, `\n`, `\r` and `\t`. The Insert text button
quotes and escapes arbitrary entered text. `#` starts a comment outside strings.

Arithmetic operators are `+ - * / % ^`; parentheses group expressions. Power is
right-associative and precedes unary minus: `-2^2` is `-4`. Comparison operators
are `== != < > <= >=`; Boolean operators are `and or not`. Conditions require a
Boolean value. `and` and `or` short-circuit. Nested conditions are supported;
loops and arbitrary code execution are not.

Functions include `abs`, `sqrt`, `min`, `max`, `round`, `floor`, `ceil`, `exp`,
`ln`, `log`, `log10`, `sin`, `cos`, `tan`, `sind`, `cosd`, `tand`, `asin`, `acos`,
`atan`, `asind`, `acosd`, `atand`, `atan2` and `atan2d`. `round(x, n)` accepts
precision from -12 to 12. `ln` and `log` are natural logarithms. Trigonometric
functions without a `d` suffix use radians; functions with the suffix use
degrees. Constants are `pi`, `e`, `true` and `false`.

Dependencies determine evaluation order, not the order of assignments. Cycles,
unknown names, incompatible quantities, non-finite results and division by zero
are errors. Errors identify the source line. Inactive branches are checked for
unknown names and dependencies; their arithmetic is evaluated only when active.
Length, angle and mass quantities remain distinct. Unit-free numeric results use
the target's document unit when assigned to a dimension. Pattern counts must be
whole numbers in the existing Pattern limits.

Source size is limited to 1 MiB, with at most 4096 assignments, 512 tokens per
line, 64 nested conditionals and 256 dependency levels. A concatenated text value
cannot exceed 1 MiB.

## Dimensions and Pattern

The catalogue uses the document's persistent `dN` identifiers. It includes
supported feature dimensions, editable Sketch dimensions, corner radii, and
linear/circular Pattern values. Locked or derived dimensions may be read but
cannot be assigned. Measured geometry and `model.mass`, `model.area`,
`model.volume` or `material.density` cannot drive dimensions, including through
intermediate parameters, because that would feed calculation outputs back into
their own geometry.

Linear Patterns expose spacing and count for each enabled direction and a
reverse count for bidirectional distribution. Circular Patterns expose count
and angle. In full-circle mode the angle is derived from the count and remains
read-only. The original Pattern validation still applies, including odd counts
for symmetric distribution and the total instance limit.

Double-clicking a Pattern copy displays the owning Pattern's parameters rather
than the source Part's dimensions. This applies to Part features, derived Bodies
and Assembly Pattern groups. Source Properties remain available through the
existing explicit source link.

An `fx` marker identifies relation-driven dimensions in the working View. Hover
shows their assignment. The marker is a display overlay and is excluded from
framebuffer output. There is no new dimension property or permanent manual-edit
lock: a later Regenerate restores the relation's result after a manual edit.

## Whole-Part colour

`color` is a reserved output for an entire Part. It requires a quoted RGB colour
in `#RRGGBB` format; it is not an ordinary user parameter. For example, with a
shared text parameter named `povrch`:

```text
if povrch == "Zn"
    color = "#A0A0A0"
elseif povrch == "nerez"
    color = "#FFFFFF"
elseif povrch == "bez povrchové úpravy"
    color = "#000000"
endif
```

Regenerate changes the Part's base, Body and face-group colours consistently,
while retaining surface roughness, metallic settings and group identities.
An unmatched value retains the previous colour. Assembly occurrence appearance
overrides continue to follow their existing rules. Assembly relations cannot
assign `color` to arbitrary inserted Parts.

## Native and command interfaces

`document.relations.get` returns `relations` as a source string and separately
returns cached parameters and physical inputs. `document.relations.set` accepts
the same string, including an empty string to clear the program. Clearing source
does not erase the last calculated parameter values or reset the last colour.

The native Part JSON version is 70 (INI version 46); Assembly JSON version is
47 (INI version 35). Relations are stored as a JSON string, not a row array.
Legacy relation-array documents are intentionally unsupported.
The INI `[UserParameterValues] Data` entry stores the complete language map as
JSON, so generated text preserves line breaks, quotes and surrounding whitespace
without creating accidental INI sections or keys.

## Verification (2026-09-30)

The Windows application and CLI compile. Seventeen focused checks passed across
the parser, dimension catalogue, Pattern annotations, Part/Assembly persistence,
transactions, metadata, dependent component operations, editor, five-language
localization, native-template creation and actual View-to-editor selection.
Regeneration tests cover geometric results, conditional colour, Unicode and
multiline text, failed calculations and Undo/Redo.

Broader checks are not all green: the complete CLI process suite stopped in its
drawing-template/DXF checks, and the complete derived-copy GUI suite exceeded
its timeout after the new Pattern dimension checks. The focused Part/Assembly
Pattern double-click test passes independently. These results do not establish
that the entire application regression suite passes.
