"""Draw the exact transverse shaft/key envelope used by the passage table."""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT/'config/lib/03-ENGINEERING-TABLES/08-KEYS-KEYWAYS/03-PASSAGE/0000-index/passage-section.svg'


def main():
    cx, cy, radius, width, depth, height = 260, 235, 138, 80, 38, 67
    left, right = cx-width/2, cx+width/2
    mouth = cy-math.sqrt(radius**2-(width/2)**2)
    floor = cy-radius+depth
    top = floor-height
    envelope = math.hypot(width/2, cy-top)
    assert abs(math.hypot(left-cx, top-cy)-envelope) < 1e-12
    assert abs(math.hypot(right-cx, top-cy)-envelope) < 1e-12
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 620 480">
<title>Passage diameter: shaft and key cross-section</title>
<desc>The concentric envelope passes through the two outer key corners. Dimensions are symbolic.</desc>
<defs>
 <pattern id="shaft" width="12" height="12" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><path d="M6 0V12" fill="none" stroke="#79828b" stroke-width="1"/></pattern>
 <pattern id="key" width="9" height="9" patternUnits="userSpaceOnUse" patternTransform="rotate(-45)"><path d="M4.5 0V9" fill="none" stroke="#526676" stroke-width="1"/></pattern>
 <marker id="arrow" viewBox="0 0 10 6" refX="10" refY="3" markerWidth="10" markerHeight="6" orient="auto-start-reverse" markerUnits="userSpaceOnUse"><path d="M10 3L0 0L0 6Z" fill="#202b35"/></marker>
</defs>
<rect width="620" height="480" fill="white"/>
<g fill="none" stroke="#202b35" stroke-width="2.2">
 <path d="M{left} {mouth} A{radius} {radius} 0 1 0 {right} {mouth} L{right} {floor} L{left} {floor}Z" fill="url(#shaft)"/>
 <rect x="{left}" y="{top}" width="{width}" height="{height}" fill="url(#key)"/>
 <circle cx="{cx}" cy="{cy}" r="{envelope}" stroke="#167998" stroke-width="2"/>
</g>
<g fill="none" stroke="#73818b" stroke-width="1" stroke-dasharray="18 4 3 4">
 <path d="M55 {cy}H465M{cx} 25V427"/>
</g>
<g fill="none" stroke="#202b35" stroke-width="1">
 <path d="M{left} {top-4}V28M{right} {top-4}V28M{cx-radius} {cy+4}V453M{cx+radius} {cy+4}V453M{cx+4} {cy-envelope}H535M{cx+4} {cy+envelope}H535M{right+4} {top}H363M{right+4} {floor}H363"/>
 <path d="M{left} 37H{right}" marker-start="url(#arrow)" marker-end="url(#arrow)"/>
 <path d="M{cx-radius} 443H{cx+radius}" marker-start="url(#arrow)" marker-end="url(#arrow)"/>
 <path d="M525 {cy-envelope}V{cy+envelope}" marker-start="url(#arrow)" marker-end="url(#arrow)"/>
 <path d="M354 {top}V{floor}" marker-start="url(#arrow)" marker-end="url(#arrow)"/>
</g>
<g fill="#202b35" font-family="Arial,sans-serif" font-size="23" text-anchor="middle">
 <text x="{cx}" y="29">b</text><text x="{cx}" y="437">⌀d</text>
 <text x="551" y="{cy+8}">⌀D</text><text x="374" y="{(top+floor)/2+8}">h</text>
</g>
</svg>'''
    TARGET.write_text(svg+'\n', encoding='utf-8')
    print(TARGET)


if __name__ == '__main__':
    main()
