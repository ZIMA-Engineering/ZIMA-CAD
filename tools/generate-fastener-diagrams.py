"""Original GPL engineering schematics; no external artwork is read or traced."""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[1] / 'config/lib/03-ENGINEERING-TABLES/05-FASTENERS'


def save(folder, name, body):
    target = ROOT / folder / '0000-index' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 700 260">
<!-- Original ZIMA Engineering schematic; GPL-3.0-or-later. -->
<rect width="700" height="260" fill="white"/>
<defs><marker id="arrow" markerWidth="7" markerHeight="7" refX="3" refY="3" orient="auto-start-reverse"><path d="M0 0L6 3L0 6Z" fill="#222"/></marker></defs>
<style>text{font:17px Arial,sans-serif;fill:#111}.outline{fill:none;stroke:#222;stroke-width:2}.axis{fill:none;stroke:#777;stroke-width:1;stroke-dasharray:8 4 2 4}.dim{fill:none;stroke:#222;stroke-width:1;marker-start:url(#arrow);marker-end:url(#arrow)}.ext{fill:none;stroke:#999;stroke-width:1}</style>
''' + body + '\n</svg>\n', encoding='utf-8')


def main():
    save('04-SCREWS', 'bolt-lengths.svg', '''
<path class="outline" d="M70 77H135V163H70Z M135 96H620V144H135 M405 96V144 M610 96L620 106 M610 144L620 134"/>
<path class="outline" d="M405 101H615 M405 139H615"/>
<path class="axis" d="M40 120H655"/>
<path class="ext" d="M135 163V235 M620 144V235 M70 77V45 M135 77V45 M405 144V190 M620 144V190 M620 96H660 M620 144H660"/>
<path class="dim" d="M135 225H620 M405 178H620 M70 48H135 M654 96V144"/>
<text x="374" y="250">l</text><text x="505" y="171">b</text><text x="98" y="40">k</text><text x="663" y="126">d</text>
''')
    # Ideal hexagon: s is across flats; e is across corners. Actual limiting
    # corner dimensions also depend on chamfers and product tolerances.
    radius = 82.0
    vertices = [(175 + radius * math.cos(i * math.tau / 6), 125 + radius * math.sin(i * math.tau / 6)) for i in range(6)]
    points = ' '.join(f'{x:.6f},{y:.6f}' for x, y in vertices)
    flat_y = radius * math.sqrt(3) / 2
    save('05-NUTS', 'hexagon-nut.svg', f'''
<g id="ideal-hexagon" data-s="{2 * flat_y:.12f}" data-e="164"><polygon class="outline" points="{points}"/>
<circle class="outline" cx="175" cy="125" r="37"/><circle class="outline" cx="175" cy="125" r="32"/>
<path class="axis" d="M65 125H285 M175 15V235"/>
<path class="dim" d="M295 {125-flat_y:.6f}V{125+flat_y:.6f} M93 231H257"/>
<path class="ext" d="M216 {125-flat_y:.6f}H305 M216 {125+flat_y:.6f}H305 M93 125V238 M257 125V238"/>
<text x="307" y="130">s</text><text x="171" y="255">e</text>
<path class="outline" d="M435 62H505L517 74V176L505 188H435L423 176V74Z M423 105H517 M423 145H517"/>
<path class="axis" d="M395 125H550"/><path class="ext" d="M423 176V235 M517 176V235"/>
<path class="dim" d="M423 224H517"/><text x="467" y="250">m</text><text x="468" y="118">d</text></g>
''')
    save('06-WASHERS', 'plain-washer.svg', '''
<g id="plain-washer" data-d1="66" data-d2="180"><circle class="outline" cx="175" cy="125" r="90"/>
<circle class="outline" cx="175" cy="125" r="33"/><path class="axis" d="M65 125H285 M175 15V235"/>
<path class="dim" d="M142 125H208 M85 230H265"/><path class="ext" d="M85 125V238 M265 125V238"/>
<text x="162" y="114">d₁</text><text x="164" y="255">d₂</text>
<path class="outline" d="M413 110H593V140H413Z M470 110V140 M536 110V140"/>
<path class="axis" d="M503 85V168"/><path class="ext" d="M593 110H640 M593 140H640"/>
<path class="dim" d="M628 110V140"/><text x="645" y="132">h</text></g>
''')


if __name__ == '__main__':
    main()
