"""Generate original, language-neutral explanatory engineering SVGs.

These analytic schematics identify symbols; they are not standard drawings or
manufacturing profiles. No downloaded or existing source artwork is traced.
"""
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / 'config/lib/03-ENGINEERING-TABLES/0000-index/diagrams'


def drawing(body):
    return '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 720 270">
<!-- Original ZIMA Engineering schematic; GPL-3.0-or-later. -->
<rect width="720" height="270" fill="white"/>
<defs><marker id="arrow" markerWidth="7" markerHeight="7" refX="3" refY="3" orient="auto-start-reverse"><path d="M0 0L6 3L0 6Z" fill="#222"/></marker></defs>
<style>text{font:18px Arial,sans-serif;fill:#111}.o{fill:none;stroke:#222;stroke-width:2}.a{fill:none;stroke:#888;stroke-width:1;stroke-dasharray:9 4 2 4}.e{fill:none;stroke:#888;stroke-width:1}.d{fill:none;stroke:#222;stroke-width:1;marker-start:url(#arrow);marker-end:url(#arrow)}</style>
''' + body + '\n</svg>\n'


def line(x1,y1,x2,y2,label,tx=None,ty=None):
    return f'<path class="d" d="M{x1} {y1}L{x2} {y2}"/><text x="{tx if tx is not None else (x1+x2)/2}" y="{ty if ty is not None else (y1+y2)/2-8}">{label}</text>'


def screw(kind):
    head = {
        'socket':'M75 65H140V175H75Z M75 101H106V139H75',
        'slotted':'M100 65H140V175H100Z M100 114H120V126H100',
        'pan':'M140 75C65 65 65 175 140 165Z',
        'button':'M140 70C65 70 65 170 140 170Z',
        'countersunk':'M70 65L140 100V140L70 175Z M70 115H95V125H70',
        'raised':'M80 65Q40 120 80 175L140 140V100Z',
        't-head':'M95 55H140V185H95Z',
        'hex':'M70 75L85 65H140V175H85L70 165Z',
    }[kind]
    left={'socket':75,'slotted':100,'pan':84,'button':84,'countersunk':70,'raised':60,'t-head':95,'hex':70}[kind]
    start=left if kind in ('countersunk','raised') else 140
    return drawing(f'<path class="o" d="{head} M140 100H620V140H140 M390 104H615 M390 136H615 M610 100L620 110 M610 140L620 130"/><path class="a" d="M35 120H665"/><path class="e" d="M{start} 170V238 M620 140V238 M390 140V195 M140 65V30 M{left} 65V30 M620 100H665 M620 140H665"/>'+line(start,230,620,230,'l')+line(390,190,620,190,'b')+line(left,35,140,35,'k')+line(650,100,650,140,'d',668,126))


def main():
    ROOT.mkdir(parents=True,exist_ok=True)
    assets={k+'-screw.svg':screw(k) for k in ['hex','socket','slotted','pan','button','countersunk','raised','t-head']}
    assets['stud.svg']=drawing('<path class="o" d="M75 95H635V145H75Z M75 100H245 M75 140H245 M425 100H635 M425 140H635"/><path class="a" d="M40 120H670"/>'+line(75,230,635,230,'l')+line(75,185,245,185,'b₁')+line(425,185,635,185,'b₂')+line(660,95,660,145,'d',675,126))
    for style,end in [('flat','V145'),('cone','L650 120L620 145'),('dog','H620V105H650V135H620V145'),('cup','H650V107L635 120L650 133V145')]:
        assets['set-'+style+'.svg']=drawing('<path class="o" d="M95 95H620'+end+'H95Z M95 104H610 M95 136H610 M95 105H130V135H95"/><path class="a" d="M55 120H680"/>'+line(95,230,650 if style!='flat' else 620,230,'l')+line(175,95,175,145,'d',185,126))
    assets['pin.svg']=drawing('<path class="o" d="M100 100L110 90H610L620 100V140L610 150H110L100 140Z"/><path class="a" d="M65 120H655"/>'+line(100,225,620,225,'l')+line(655,90,655,150,'d',668,126)+line(100,55,110,55,'c',90,40))
    assets['clevis.svg']=drawing('<path class="o" d="M70 70H135V170H70Z M135 95H620V145H135 M585 95V145 M575 95V145"/><path class="a" d="M40 120H665"/>'+line(135,225,620,225,'l')+line(655,95,655,145,'d',670,127)+line(70,40,135,40,'k')+'<text x="560" y="79">d₁</text><text x="57" y="195">dₖ</text>')
    assets['taper-pin.svg']=drawing('<path class="o" d="M100 105L620 80V160L100 135Z"/><path class="a" d="M65 120H655"/>'+line(100,225,620,225,'l')+line(70,105,70,135,'d',42,128)+line(655,80,655,160,'D',675,128))
    assets['split-pin.svg']=drawing('<path class="o" d="M120 120C20 40 20 200 120 120 M120 113H610V120H120 M120 127H660V120H120"/><path class="a" d="M30 120H690"/>'+line(120,225,610,225,'l')+line(530,113,530,127,'d',548,104))
    assets['spring-pin.svg']=drawing('<path class="o" d="M90 80H590V160H90Z M90 100H590 M90 140H590"/><path class="a" d="M60 120H630"/>'+line(90,230,590,230,'l')+line(630,80,630,160,'d₁',649,126)+'<text x="280" y="126">s</text>')
    assets['key.svg']=drawing('<path class="o" d="M85 90H520Q565 90 565 120Q565 150 520 150H85Q40 150 40 120Q40 90 85 90Z M605 90H675V150H605Z"/>'+line(40,225,565,225,'l')+line(585,90,585,150,'b',566,127)+line(690,90,690,150,'h',701,127)+'<text x="507" y="78">r</text>')
    assets['keyway.svg']=drawing('<circle class="o" cx="180" cy="120" r="83"/><path class="o" d="M155 64V34H205V64 M155 64H205"/><path class="a" d="M65 120H295 M180 10V235"/>'+line(155,25,205,25,'b',174,18)+line(97,230,263,230,'D')+line(300,37,300,64,'t',315,62)+'<path class="o" d="M430 65H640V190H430Z M430 90H640 M475 90V65 M580 90V65"/><text x="505" y="120">t₁</text>')
    assets['woodruff.svg']=drawing('<path class="o" d="M90 80H310A110 110 0 0 1 90 80Z M460 90H520V190H460Z"/>'+line(90,235,310,235,'d')+line(350,80,350,190,'h',367,139)+line(460,230,520,230,'b'))
    assets['bearing.svg']=drawing('<path class="o" d="M85 40H200V225H85Z M385 40H500V225H385Z M85 80H200 M85 185H200 M385 80H500 M385 185H500"/><circle class="o" cx="142" cy="130" r="35"/><circle class="o" cx="442" cy="130" r="35"/><path class="a" d="M40 130H545"/>'+line(45,40,45,225,'D',19,138)+line(555,80,555,185,'d',573,138)+line(85,255,200,255,'B',135,249))
    assets['o-ring.svg']=drawing('<circle class="o" cx="180" cy="130" r="90"/><circle class="o" cx="180" cy="130" r="65"/><path class="a" d="M65 130H295 M180 20V240"/>'+line(115,130,245,130,'d₁')+'<circle class="o" cx="520" cy="130" r="50"/><text x="503" y="55">4:1</text>'+line(470,210,570,210,'d₂'))
    assets['hole.svg']=drawing('<path class="o" d="M65 75H615V190H65Z M300 75V190 M380 75V190 M260 75V110H300 M420 75V110H380"/><path class="a" d="M340 35V235"/>'+line(300,230,380,230,'D')+line(260,40,420,40,'D₁')+line(660,75,660,110,'t',675,105))
    assets['chamfer.svg']=drawing('<path class="o" d="M90 195V95L160 25H570V195Z M90 25H160"/><path class="e" d="M90 25V95 M160 25V5"/>'+line(90,12,160,12,'a',123,8)+line(65,25,65,95,'b',41,65)+'<path class="a" d="M90 95H200"/><text x="177" y="70">α</text>')
    assets['pipe.svg']=drawing('<circle class="o" cx="180" cy="130" r="90"/><circle class="o" cx="180" cy="130" r="62"/>'+line(90,235,270,235,'D')+line(118,130,242,130,'d')+'<text x="240" y="75">t</text><path class="o" d="M410 80H650V180H410Z M410 100H650 M410 160H650"/>'+line(410,230,650,230,'L'))
    assets['fillet-weld.svg']=drawing('<path class="o" d="M80 190H620V220H80Z M300 40H330V190H300Z M330 120L400 190H330Z"/>'+line(420,190,420,120,'z',438,160)+line(330,245,400,245,'z')+'<path class="d" d="M330 190L365 155"/><text x="340" y="162">a</text>')
    assets['beam.svg']=drawing('<path class="o" d="M100 70H620V110H100Z M100 110L75 145H125Z M590 110L570 145H610Z"/><path class="d" d="M360 20V70"/><text x="380" y="40">F</text>'+line(100,225,620,225,'L')+'<path class="a" d="M100 110Q360 175 620 110"/><text x="375" y="168">δ</text>')
    assets['fit.svg']=drawing('<path class="o" d="M100 45H590V85H100Z M100 185H590V225H100Z M100 100H590V170H100Z"/><path class="a" d="M60 135H630"/>'+line(650,85,650,185,'D',670,138)+line(70,100,70,170,'d',42,140)+'<text x="290" y="70">ES / EI</text><text x="290" y="150">es / ei</text>')
    for name,svg in assets.items():
        (ROOT/name).write_text(svg,encoding='utf-8',newline='\n')
    print(f'{len(assets)} original reference schematics')


if __name__=='__main__':main()
