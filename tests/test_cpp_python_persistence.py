import json
import os
import shutil
import subprocess
import uuid
from pathlib import Path

import pytest

from zima_cad.storage import load_part_document


ROOT = Path(__file__).resolve().parents[1]
PYTHON = ROOT / "runtime" / "linux" / "python" / "bin" / "python"
DEFAULT_EMITTER = ROOT / "build" / "cpp-debug" / (
    "zima_cpp_cross_language_persistence_emitter"
)
EMITTER = Path(os.environ["ZIMA_CPP_CROSS_LANGUAGE_EMITTER"]) if (
    "ZIMA_CPP_CROSS_LANGUAGE_EMITTER" in os.environ
) else DEFAULT_EMITTER


def _find_parameter(document, key):
    return _find_object(document, key).parameters[key]


def _find_object(document, key):
    # Sketch/construction entities are nested one level below their owning
    # Container, matching Python's own container/entity tree shape, so
    # parameter lookups must walk the whole tree rather than only direct
    # children of the document root.
    pending = list(document.root.children)
    while pending:
        obj = pending.pop()
        if key in obj.parameters:
            return obj
        pending.extend(obj.children)
    raise AssertionError(f"missing persisted parameter {key!r}")


@pytest.mark.skipif(
    not EMITTER.exists(),
    reason="build the C++ persistence emitter first",
)
def test_cpp_documents_load_in_bundled_python():
    emitter = Path(
        os.environ.get("ZIMA_CPP_CROSS_LANGUAGE_EMITTER", str(DEFAULT_EMITTER))
    )
    output = ROOT / f".cpp-python-persistence-{uuid.uuid4().hex}"
    try:
        result = subprocess.run(
            [str(emitter), str(output)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        assert result.returncode == 0, result.stderr or result.stdout

        part = load_part_document(output / "cpp_part.prtz")
        assert part.document_settings["type"] == "part"
        assert part.document_settings["document_id"] == "cpp-part-persistence-001"
        assert part.document_settings["name"] == "C++ Persistence Part"
        assert part.user_parameters["material"] == "Steel"
        sketch = _find_object(part, "cpp_sketch")
        sketch_data = json.loads(sketch.parameters["cpp_sketch"])
        assert sketch_data["id"] == "cpp-sketch-001"
        assert sketch_data["plane"] == "xz"
        assert len(sketch_data["segments"]) == 4
        assert len(sketch_data["circles"]) == 1
        assert _find_parameter(part, "cpp_history")

        assembly = load_part_document(output / "cpp_assembly.asmz")
        assert assembly.document_settings["type"] == "assembly"
        assert assembly.document_settings["document_id"] == "cpp-assembly-001"
        payload = json.loads(assembly.document_settings["cpp_assembly"])
        assert payload["name"] == "C++ Nested Assembly Root"
        occurrence = payload["components"][0]
        assert occurrence["occurrence_id"] == "cpp-nested-occurrence-001"
        assert occurrence["source_document_id"] == "cpp-nested-assembly-001"
        nested_part = occurrence["nested_snapshot"][0]
        assert nested_part["occurrence_id"] == "cpp-part-occurrence-001"
        assert nested_part["source_document_id"] == "cpp-part-persistence-001"

        drawing = load_part_document(output / "cpp_drawing.drwz")
        assert drawing.document_settings["type"] == "drawing"
        assert drawing.document_settings["document_id"] == "cpp-drawing-001"
        drawing_payload = json.loads(drawing.document_settings["param.cpp_drawing"])
        sheet = drawing_payload["sheets"][0]
        assert sheet["id"] == "cpp-sheet-001"
        view = sheet["views"][0]
        assert view["id"] == "cpp-view-001"
        assert view["source_document_id"] == "cpp-part-persistence-001"
    finally:
        shutil.rmtree(output, ignore_errors=True)
