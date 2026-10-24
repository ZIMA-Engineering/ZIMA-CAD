"""Install the independently verified coordinate-contract authoring outputs.

The default is read-only. The factory baseline goes into Git; independently
converted private library edits remain in isolated verification storage.
Private working files are never replaced by this installer. No legacy reader,
required sidecar or user-data publication is introduced.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from importlib.util import spec_from_file_location, module_from_spec

ROOT=Path(__file__).resolve().parents[1]
spec=spec_from_file_location('coordinate_checks',ROOT/'tools/verify-coordinate-factory.py')
checks=module_from_spec(spec);spec.loader.exec_module(checks)

def git(*args,**kwargs):
    return subprocess.check_output(['git',*args],cwd=ROOT,**kwargs)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,default=ROOT/'build/coordinate-factory')
    parser.add_argument('--private-evidence',type=Path,required=True)
    parser.add_argument('--install',action='store_true')
    args=parser.parse_args();stage=args.stage.resolve()
    report=json.loads((stage/'verification-factory.json').read_text())
    inventory=json.loads((stage/'inventory-factory.json').read_text())
    commit=git('rev-parse','HEAD').decode().strip()
    assert report['commit']==commit and report['total']==report['passed']==1185 and not report['partial']
    assert report['native_sha256']==checks.checks.sha(ROOT/'build/cpp-coordinate-release/zima_refresh_start_templates.exe')
    assert json.loads((stage/'ownership.json').read_text())=={'repository':str(ROOT),'commit':commit,'kind':'coordinate-contract-36'}
    assert not git('diff','--cached','--name-only').strip(),'Keep unrelated staged changes separate'
    private=json.loads(args.private_evidence.read_text())
    private={Path(x['source']).resolve():x for x in private}
    originals={x['path']:x for x in inventory};rows=[]
    for row in report['assets']:
        name=row['path'];source=ROOT/name;output=stage/'verified'/name
        assert checks.checks.sha(source)==originals[name]['working_sha256'],'Working asset changed: '+name
        assert checks.checks.sha(output)==row['output_sha256'],'Verified output changed: '+name
        baseline=git('show',commit+':'+name)
        actual=source.read_bytes()
        authored=actual.replace(b'\r\n',b'\n')!=baseline.replace(b'\r\n',b'\n')
        if authored:
            proof=private[source.resolve()]
            assert proof['before_sha256']==checks.checks.sha(source) and proof['exit']==0
            assert proof['exact_reference_geometry'] and proof['identities']
            assert Path(proof['backup']).is_file() and Path(proof['output']).is_file()
        rows.append({'path':name,'output_sha256':row['output_sha256'],'before_sha256':checks.checks.sha(source),'private':authored})
    assert len(rows)==1185
    if not args.install:
        print('Validated 1185 factory outputs; private authored assets:',sum(x['private'] for x in rows));return
    (stage/'installation-intent.json').write_text(json.dumps({'commit':commit,'assets':rows},indent=2))
    clean=[]
    for row in rows:
        name=row['path'];output=stage/'verified'/name
        if row['private']:
            blob=git('hash-object','-w','--path='+name,str(output)).decode().strip()
            git('update-index','--add','--cacheinfo','100644,'+blob+','+name)
        else:
            shutil.copyfile(output,ROOT/name);clean.append(name)
    pathspec=stage/'install-paths.nul';pathspec.write_bytes(b'\0'.join(x.encode() for x in clean)+b'\0')
    git('add','--pathspec-from-file='+str(pathspec),'--pathspec-file-nul')
    for row in rows:
        name=row['path'];expected=(stage/'verified'/name).read_bytes();indexed=git('show',':'+name)
        assert indexed.replace(b'\r\n',b'\n')==expected.replace(b'\r\n',b'\n'),'Staged content differs: '+name
        if row['private']:
            proof=private[(ROOT/name).resolve()]
            assert checks.checks.sha(ROOT/name)==row['before_sha256']
            # Private working bytes stay untouched; installing their verified
            # authoring conversion is a separate explicitly approved action.
        row['committed_sha256']=hashlib.sha256(indexed).hexdigest()
    (stage/'installation.json').write_text(json.dumps({'commit':commit,'installed_and_staged':True,'assets':rows},indent=2))
    print('Installed and staged 1185 verified factory assets; private working files remain byte-identical')

if __name__=='__main__':main()
