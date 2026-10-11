"""Provision one complete official Qt 6.11.0 MSVC SDK; never mix Qt kits."""
import argparse
import hashlib
import json
import re
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

VERSION = '6.11.0'
BASE = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6110/qt6_6110_msvc2022_64/'
ENGINE = 'https://download.qt.io/online/qtsdkrepository/windows_x86/extensions/qtwebengine/6110/msvc2022_64/'

def digest(path, algorithm='sha256'):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()

def packages(url):
    with urllib.request.urlopen(url+'Updates.xml', timeout=60) as response:
        return ET.fromstring(response.read()).findall('PackageUpdate')

def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--sevenzip',type=Path,required=True)
    args=parser.parse_args()
    root=args.root.resolve()
    if root==Path(root.anchor) or len(str(root))>60:raise ValueError('Use a short dedicated SDK directory')
    root.mkdir(parents=True,exist_ok=True)
    archives=root/'archives';archives.mkdir(exist_ok=True)
    selected=[]
    names={'qt.qt6.6110.win64_msvc2022_64'}
    for module in ('qtshadertools','qtlanguageserver','qtwebchannel','qtpositioning','qtmultimedia'):
        names.add('qt.qt6.6110.addons.'+module+'.win64_msvc2022_64')
    for package in packages(BASE):
        if package.findtext('Name') in names:selected.append((BASE,package))
    for package in packages(ENGINE):
        if package.findtext('Name')=='extensions.qtwebengine.6110.win64_msvc2022_64':selected.append((ENGINE,package))
    if len(selected)!=len(names)+1:raise ValueError('The official repository lacks required matching SDK modules')
    records=[]
    for base,package in selected:
        revision=package.findtext('Version');name=package.findtext('Name')
        if not re.fullmatch(r'6\.11\.0-\d+-\d{12}',revision):raise ValueError('Qt module version mismatch')
        for member in package.findtext('DownloadableArchives').split(','):
            member=member.strip()
            if not re.fullmatch(r'[A-Za-z0-9_.-]+\.7z',member):raise ValueError('Unsafe SDK archive name')
            if member.startswith('qtdoc-'):continue
            filename=revision+member;url=base+name+'/'+filename
            archive=archives/filename
            with urllib.request.urlopen(url+'.sha1',timeout=60) as response:
                expected=response.read().decode().strip().split()[0]
            if not archive.is_file() or digest(archive,'sha1')!=expected:
                print('Downloading',member,flush=True)
                partial=archive.with_suffix('.partial')
                urllib.request.urlretrieve(url,partial)
                if digest(partial,'sha1')!=expected:raise ValueError('Official archive checksum mismatch')
                partial.replace(archive)
            listing=subprocess.check_output([args.sevenzip,'l','-slt','-sccUTF-8',archive]).decode('utf-8').replace('\r\n','\n')
            entries=listing.split('----------\n',1)[-1].split('\n\n')
            for entry in entries:
                paths=[line[7:] for line in entry.splitlines() if line.startswith('Path = ')]
                for text in paths:
                    path=PurePosixPath(text.replace('\\','/'))
                    if path.is_absolute() or '..' in path.parts or ':' in text:
                        raise ValueError('Unsafe SDK archive member: '+text)
                    if len(str(root/path))>259:raise ValueError('SDK member exceeds Windows path limit: '+text)
                if 'Symbolic Link = ' in entry or 'Hard Link = ' in entry:
                    raise ValueError('SDK archive links are not permitted')
            print('Extracting',member,flush=True)
            with tempfile.TemporaryDirectory(prefix='qt-',dir=root) as temporary:
                subprocess.run([args.sevenzip,'x','-y','-bd','-o'+temporary,archive],check=True,stdout=subprocess.DEVNULL)
                for path in Path(temporary).rglob('*'):
                    if not path.is_file():continue
                    destination=root/path.relative_to(temporary)
                    destination.parent.mkdir(parents=True,exist_ok=True)
                    if destination.exists():
                        if digest(destination)!=digest(path):raise ValueError('Conflicting SDK module file: '+str(destination))
                    else:shutil.copy2(path,destination)
            records.append({'url':url,'sha1':expected,'sha256':digest(archive)})
    kit=root
    actual=subprocess.check_output([kit/'bin/qmake.exe','-query','QT_VERSION']).decode().strip()
    if actual!=VERSION:raise ValueError('Provisioned Qt version mismatch')
    (kit/'zima-sdk.json').write_text(json.dumps({'name':'Qt','version':VERSION,'architecture':'msvc2022_64','archives':records},indent=2)+'\n',encoding='utf-8')
    print('Verified complete Qt SDK:',kit,flush=True)

if __name__=='__main__':main()
