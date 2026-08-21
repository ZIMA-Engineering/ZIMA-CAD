import json
from pathlib import Path
from tempfile import TemporaryDirectory

from zima_cad.storage import load_part_document, save_part_document


FIXTURES = Path(__file__).parent / "fixtures" / "cross_language"


def test_python_fixture_set_is_current_ini_only():
    assert {path.name for path in FIXTURES.iterdir()} == {
        "part.prtz", "nested.asmz", "drawing.drwz",
    }
    for path in FIXTURES.iterdir():
        text = path.read_text(encoding="utf-8")
        assert "format_version=11" in text
        assert "[" in text
        assert not text.lstrip().startswith("{")
    loaded = load_part_document(FIXTURES / "part.prtz")
    assert loaded.document_settings["document_id"] == "part-fixture-001"
    assert loaded.document_settings["type"] == "part"


def test_python_loads_cpp_assembly_and_drawing_payloads():
    occurrence = {
        "occurrence_id": "occ-001",
        "name": "Occurrence",
        "source_document_id": "part-001",
        "source_path": "part.prtz",
        "source_kind": "part",
        "suppressed": False,
        "visible": True,
        "grounded": False,
        "placement": {
            "x": 0.0, "y": 0.0, "z": 0.0,
            "rotation_x": 0.0, "rotation_y": 0.0, "rotation_z": 0.0,
        },
    }
    drawing = {
        "format": "zima-cad-drawing",
        "version": 2,
        "document_id": "drawing-001",
        "name": "Drawing",
        "sheets": [{
            "id": "sheet-001",
            "name": "Sheet 1",
            "format": "A4",
            "views": [{
                "id": "view-001",
                "source_document_id": "part-001",
            }],
        }],
    }
    with TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "assembly.asmz").write_text(
            "\n".join([
                "[Document]",
                "format_version=11",
                "type=assembly",
                "document_id=assembly-001",
                "name=Assembly",
                "",
                "[Containers]",
                "items=assembly-001,occ-001",
                "",
                "[Container.assembly-001]",
                "id=assembly-001",
                "name=Assembly",
                "kind=container",
                "TYPE=ASSEMBLY",
                "param.cpp_kind=assembly",
                "",
                "[Container.occ-001]",
                "id=occ-001",
                "name=Occurrence",
                "kind=container",
                "TYPE=OCCURRENCE",
                f"param.cpp_data={json.dumps(occurrence, separators=(',', ':'))}",
                "",
            ]),
            encoding="utf-8",
        )
        (root / "drawing.drwz").write_text(
            "\n".join([
                "[Document]",
                "format_version=11",
                "type=drawing",
                "document_id=drawing-001",
                "name=Drawing",
                f"param.cpp_drawing={json.dumps(drawing, separators=(',', ':'))}",
                "",
                "[Containers]",
                "items=",
                "",
            ]),
            encoding="utf-8",
        )
        assembly = load_part_document(root / "assembly.asmz")
        component = assembly.history_objects()[0]
        assert assembly.document_settings["document_id"] == "assembly-001"
        assert component.container_type.value == "COMPONENT"
        assert component.parameters["source_document_id"] == "part-001"
        loaded_drawing = load_part_document(root / "drawing.drwz")
        sheets = json.loads(loaded_drawing.document_settings["drawing_sheets"])
        assert sheets[0]["views"][0]["source_document_id"] == "part-001"


def test_cpp_assembly_mates_and_cuts_map_and_round_trip():
    dependent = {
        "kind": "face", "instance_path": "occ-001", "owner_id": "face-owner",
        "semantic_key": "face-key",
    }
    prerequisite = {
        "kind": "face", "instance_path": "occ-002", "owner_id": "face-owner-2",
        "semantic_key": "face-key-2",
    }
    mate = {
        "mate_id": "mate-001", "name": "Fixed", "kind": "plane",
        "dependent": dependent, "prerequisite": prerequisite,
        "offset": 1.25, "angle_degrees": 0.0, "lower_limit": -2.0,
        "upper_limit": 3.0, "flipped": True, "status": "solved",
        "suppressed": False,
    }
    cut = {
        "id": "cut-001", "feature_id": "cut-001:feature",
        "feature_parent_id": "cut-001", "name": "Assembly Cut",
        "kind": "extrusion", "suppressed": True, "targets": ["occ-002"],
    }
    root_payload = {
        "format": "zima-cad-cpp", "type": "assembly",
        "components": [], "mates": [mate], "cuts": [cut],
    }
    occurrence = {
        "occurrence_id": "occ-001", "name": "One",
        "source_document_id": "part-001", "source_path": "one.prtz",
        "source_kind": "part", "suppressed": False, "visible": True,
        "grounded": False, "placement": {},
    }
    with TemporaryDirectory() as directory:
        path = Path(directory) / "assembly.asmz"
        path.write_text("\n".join([
            "[Document]", "format_version=11", "type=assembly",
            "document_id=assembly-001", "name=Assembly", "",
            "[Containers]", "items=assembly-001,occ-001",
            "", "[Container.assembly-001]", "id=assembly-001",
            "name=Assembly", "kind=container", "TYPE=ASSEMBLY",
            f"param.cpp_assembly={json.dumps(root_payload)}", "",
            "[Container.occ-001]", "id=occ-001", "name=One",
            "kind=container", "TYPE=OCCURRENCE",
            f"param.cpp_data={json.dumps(occurrence)}", "",
        ]), encoding="utf-8")
        loaded = load_part_document(path)
        component = loaded.history_objects()[0]
        assert json.loads(loaded.document_settings["assembly_mates"])[0] == mate
        row = json.loads(component.parameters["assembly_mates"])[0]
        assert row["target_occurrence_id"] == "occ-002"
        assert row["flipped"] is True
        assert json.loads(loaded.document_settings["assembly_cuts"])[0] == cut
        output = Path(directory) / "roundtrip.asmz"
        save_part_document(loaded, output)
        roundtrip = load_part_document(output)
        assert json.loads(roundtrip.document_settings["assembly_mates"])[0] == mate
        assert json.loads(roundtrip.document_settings["assembly_cuts"])[0] == cut
