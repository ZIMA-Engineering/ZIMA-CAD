"""Render current factory symbols using their native default variant, without saving them."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=ROOT/'build/cpp-windows-release/zima_symbol_catalog_tool.exe')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    sources = sorted((ROOT/'config/symbols').rglob('*.symz'))
    assert sources, 'No current factory symbols found'
    for source in sources:
        digest = hashlib.sha256(source.read_bytes()).digest()
        output = source.parent/'0000-index/thumbnails'/(source.name+'.png')
        if not args.check:
            output.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run([str(args.tool), '--thumbnail', str(source), str(output)], check=True,
                           cwd=ROOT, env={**os.environ, 'QT_QPA_PLATFORM':'offscreen'})
        image = output.read_bytes()
        assert image[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',image[16:24]) == (640,480), output
        assert hashlib.sha256(source.read_bytes()).digest() == digest, f'Source changed: {source}'
        print(output.relative_to(ROOT))
    print(f'{len(sources)} current symbol thumbnails verified; native source files unchanged')

if __name__ == '__main__':
    main()
