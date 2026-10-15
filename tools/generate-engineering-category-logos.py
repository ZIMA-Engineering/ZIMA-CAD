"""Maintain factory engineering-category branding using the existing symbol style."""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]/'config/lib/03-ENGINEERING-TABLES'
ARTWORK={
 '01-GENERAL-STANDARDS':'<path d="M14 12h30l7 7v34H14zM44 12v9h7M21 28h22M21 35h22M21 42h14"/>',
 '02-TOLERANCES':'<path d="M12 14v39M12 24h37V14M20 24v16h24V24M12 48h37M25 44v8M36 44v8"/>',
 '03-THREADS':'<path d="M22 10h20v44H22zM22 16l20 8M22 25l20 8M22 34l20 8M22 43l20 8M16 10h32"/>',
 '04-SHAFTS-SPLINES-GEARS':'<path d="M8 27h12v-8h24v8h12v10H44v8H20v-8H8zM26 19v26M38 19v26M20 23h-5M20 41h-5M44 23h5M44 41h5"/>',
 '05-FASTENERS':'<path d="M22 11h20l7 10-7 10H22l-7-10zM26 31v23h12V31M26 39h12M26 47h12"/>',
 '06-FASTENER-REQUIREMENTS':'<path d="M32 9l20 7v16c0 12-12 20-20 24-8-4-20-12-20-24V16zM22 32l7 7 14-16"/>',
 '07-PINS-RETAINERS-DOWELS':'<path d="M18 12h9v40h-9zM18 19h9M18 45h9M43 13c-11 0-16 11-10 20l6 9v12M43 13c11 0 16 11 10 20l-6 9v12M39 47h8"/>',
 '08-KEYS-KEYWAYS':'<path d="M12 26l9-9h32v13l-9 9H12zM12 26h32v13M44 26l9-9M18 46h30M24 42v8M42 42v8"/>',
 '09-BEARINGS':'<circle cx="32" cy="32" r="23"/><circle cx="32" cy="32" r="11"/><circle cx="32" cy="15" r="4"/><circle cx="49" cy="32" r="4"/><circle cx="32" cy="49" r="4"/><circle cx="15" cy="32" r="4"/>',
 '10-SEALS':'<circle cx="32" cy="32" r="23"/><circle cx="32" cy="32" r="15"/><path d="M32 9v8M32 47v8M9 32h8M47 32h8M21 22l7 10-7 10M43 22l-7 10 7 10"/>',
 '11-PORTS-CONNECTIONS':'<path d="M9 25h12v-6h23v6h11v14H44v6H21v-6H9zM26 19v26M39 19v26M13 30h8M44 34h8"/>',
 '12-PIPES-VALVES':'<path d="M8 24h18v-5h12v5h18v16H38v5H26v-5H8zM26 24l12 16M38 24L26 40M32 19V9M23 9h18"/>',
 '13-WELDING-BRAZING':'<path d="M9 48h46M17 48V25l23 23M34 12l14 14-8 8-14-14zM28 32l-6 7M39 40l5 5M23 20l-6-5"/>',
 '14-MANUFACTURING':'<path d="M24 9h16v13H24zM21 22h22v15H21zM25 25l7 9M34 25l6 9M32 37v8M11 49h42v8H11zM17 43v6M47 43v6"/>',
 '15-SEMI-FINISHED':'<path d="M11 20l12-8h30v13H25v28H11zM11 20h14M25 20l12-8M25 25h28M11 53h14M35 36h18v17H35z"/>',
 '16-MATERIALS':'<path d="M22 11h20l11 19-11 20H22L11 30zM22 11l10 19-10 20M42 11L32 30l10 20M11 30h42"/><circle cx="32" cy="30" r="4"/>',
 '17-TECHNICAL-DRAWING':'<path d="M13 9h30l8 8v38H13zM43 9v10h8M21 29h20v14H21zM18 48h27M21 45v6M41 45v6M21 23h20"/>',
 '51-CALCULATIONS':'<rect x="15" y="8" width="34" height="48" rx="4"/><path d="M21 15h22v11H21zM22 35h7M25.5 32v6M36 35h7M22 46h7M36 43l7 7M43 43l-7 7"/>',
}

def artwork(body):
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect x="3" y="3" width="58" height="58" rx="10" fill="#4AA3E8" fill-opacity=".12"/><g fill="none" stroke="#428BCC" stroke-width="3" stroke-linecap="round" stroke-linejoin="round">'+body+'</g></svg>\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    directories={p.name for p in ROOT.iterdir() if p.is_dir() and p.name!='0000-index'}
    assert directories==set(ARTWORK),'Every top-level category needs explicit artwork'
    for name,body in ARTWORK.items():
        index=ROOT/name/'0000-index'
        path=index/'metadata.ini'
        original=path.read_text(encoding='utf-8-sig')
        lines=original.splitlines()
        begin=lines.index('[Directory]')+1
        end=next((i for i in range(begin,len(lines)) if lines[i].startswith('[')),len(lines))
        section=[line for line in lines[begin:end] if line.split('=',1)[0] not in {'Logo','LogoMode'}]
        for language in ['cs','en','de','fr','ru']:
            assert any(line.startswith('Label\\'+language+'=') and line.split('=',1)[1].strip() for line in section),(name,language)
        text='\n'.join([*lines[:begin],*section,'LogoMode=logo-text','Logo=logo-text.svg',*lines[end:]])+'\n'
        svg=artwork(body)
        ET.fromstring(svg)
        if args.check:
            assert original==text,path
            assert (index/'logo-text.svg').read_text(encoding='utf-8')==svg,name
        else:
            if text!=original:path.write_text(text,encoding='utf-8',newline='\n')
            (index/'logo-text.svg').write_text(svg,encoding='utf-8',newline='\n')
    print('18 category logos; native logo-text presentation; all five directory labels verified')

if __name__=='__main__':main()
