"""Draw original, language-neutral engineering schematics from geometry.

No source-standard artwork is read or traced. SVGs are GPL-3.0-or-later.
"""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[1] / 'config/lib/03-ENGINEERING-TABLES/04-SHAFTS-SPLINES-GEARS'


def save(folder, name, content):
    target = ROOT / folder / '0000-index' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 700 260">
<!-- Original ZIMA Engineering schematic; GPL-3.0-or-later. -->
<rect width="700" height="260" fill="white"/>
<defs><marker id="arrow" markerWidth="7" markerHeight="7" refX="3" refY="3" orient="auto-start-reverse"><path d="M0 0L6 3L0 6Z" fill="#222"/></marker></defs>
<style>text{font:17px Arial,sans-serif;fill:#111}.outline{fill:none;stroke:#222;stroke-width:2}.axis{fill:none;stroke:#666;stroke-width:1;stroke-dasharray:8 4 2 4}.dim{fill:none;stroke:#222;stroke-width:1;marker-start:url(#arrow);marker-end:url(#arrow)}.extension{fill:none;stroke:#888;stroke-width:1}</style>
''' + content + '\n</svg>\n', encoding='utf-8')


def main():
    save('01-SHAFT-END', 'shaft-ends.svg', '''
<path class="outline" d="M35 75H310V165H35 M35 65V175 M390 65L655 92V148L390 175 M390 55V185"/>
<path class="axis" d="M15 120H335 M365 120H680"/>
<path class="extension" d="M35 180V220 M310 170V220 M390 185V220 M655 150V220 M310 75H347 M310 165H347 M390 65H374 M390 175H374 M655 92H677 M655 148H677"/>
<path class="dim" d="M35 210H310 M390 210H655 M340 75V165 M377 65V175 M675 92V148"/>
<text x="165" y="238">l₁</text><text x="520" y="238">l₂</text><text x="320" y="125">d</text><text x="351" y="125">d</text><text x="680" y="125">d₂</text>
<text x="140" y="42">l₁, d</text><text x="485" y="42">l₂, d, d₂</text>
''')
    # Six straight-sided teeth, formed by rectangles intersecting two circles.
    minor, major, half_width = 70.0, 96.0, 12.0
    root_angle = math.asin(half_width / minor)
    outer_x = math.sqrt(major * major - half_width * half_width)
    root_x = math.sqrt(minor * minor - half_width * half_width)
    points = []
    for tooth in range(6):
        angle = tooth * math.tau / 6
        for x, y in [(root_x, -half_width), (outer_x, -half_width), (outer_x, half_width), (root_x, half_width)]:
            points.append((175 + x * math.cos(angle) - y * math.sin(angle), 125 + x * math.sin(angle) + y * math.cos(angle)))
        for step in range(1, 21):
            theta = angle + root_angle + (math.tau / 6 - 2 * root_angle) * step / 20
            points.append((175 + minor * math.cos(theta), 125 + minor * math.sin(theta)))
    contour = ' '.join(f'{x:.3f},{y:.3f}' for x, y in points)
    save('04-STRAIGHT', 'straight-spline.svg', f'''
<polygon class="outline" points="{contour}"/>
<circle class="axis" cx="175" cy="125" r="70"/><circle class="axis" cx="175" cy="125" r="96"/>
<path class="axis" d="M65 125H285 M175 15V235"/>
<path class="dim" d="M105 125H245 M79 233H271 M310 113V137"/>
<path class="extension" d="M79 125V240 M271 125V240 M270 113H317 M270 137H317"/>
<text x="150" y="115">d</text><text x="162" y="254">D</text><text x="323" y="130">b</text>
<text x="395" y="93">z = 6</text><text x="395" y="128">d &lt; D</text><text x="395" y="163">d, D, b</text>
''')
    # Analytic involute flank: t=tan(alpha_r), r=rb*sqrt(1+t²).
    z, pitch, addendum, root, alpha = 20, 88.0, 96.8, 77.0, math.radians(20)
    base = pitch * math.cos(alpha)
    inv_alpha = math.tan(alpha) - alpha
    flank, outline = [], []
    tmax = math.sqrt((addendum / base) ** 2 - 1)
    for i in range(41):
        t = tmax * i / 40
        r = base * math.sqrt(1 + t * t)
        theta = math.pi / (2 * z) + inv_alpha - (t - math.atan(t))
        flank.append((r, theta))
    for j in range(z):
        centre = j * math.tau / z
        pairs = [(root, -flank[0][1])] + [(r, -theta) for r, theta in flank]
        tip_angle = flank[-1][1]
        pairs += [(addendum, -tip_angle + 2 * tip_angle * i / 8) for i in range(1, 9)]
        pairs += [(r, theta) for r, theta in reversed(flank)] + [(root, flank[0][1])]
        pairs += [(root, flank[0][1] + (math.tau / z - 2 * flank[0][1]) * i / 8) for i in range(1, 9)]
        outline += [(175 + r * math.cos(centre + theta), 125 + r * math.sin(centre + theta)) for r, theta in pairs]
    contour = ' '.join(f'{x:.3f},{y:.3f}' for x, y in outline)
    save('06-GEARS', 'involute-gear.svg', f'''
<g id="involute-gear" data-z="20" data-module="8.8" data-alpha="20" data-pitch-radius="88" data-base-radius="{base:.12f}">
<polygon class="outline" points="{contour}"/>
<circle class="axis" cx="175" cy="125" r="88"/><circle class="axis" cx="175" cy="125" r="{base:.12f}"/>
<circle class="outline" cx="175" cy="125" r="20"/>
<path class="axis" d="M65 125H285 M175 15V235"/>
<path class="dim" d="M87 230H263"/><path class="extension" d="M87 125V236 M263 125V236"/>
<text x="168" y="254">d</text>
<text x="370" y="58">α = 20°, x = 0, β = 0°</text>
<text x="370" y="94">d = mz</text><text x="370" y="128">d_b = d cos α</text>
<text x="370" y="162">p = πm</text><text x="370" y="196">d_a = m(z + 2)</text>
</g>''')


if __name__ == '__main__':
    main()
