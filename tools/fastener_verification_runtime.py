"""Pin one immutable native checker beside its existing Windows dependencies."""
import hashlib
import json
from pathlib import Path
import uuid

ROOT=Path(__file__).resolve().parents[1]


def pin_checker(output):
    source=ROOT/'build/cpp-windows-release/fastener-runtime/zima_cpp_fastener_library_contract_tests.exe'
    data=source.read_bytes();digest=hashlib.sha256(data).hexdigest()
    target=source.with_name('fastener-check-'+digest[:12]+'-'+uuid.uuid4().hex[:8]+source.suffix)
    with target.open('xb') as stream:stream.write(data)
    assert hashlib.sha256(target.read_bytes()).hexdigest()==digest
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    (output/'checker-runtime.json').write_text(json.dumps(dict(source=str(source),
        executable=str(target),sha256=digest),indent=2),encoding='utf-8')
    return target
