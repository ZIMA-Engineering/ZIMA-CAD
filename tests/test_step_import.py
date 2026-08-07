from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from OCC.Core.BRep import BRep_Builder
from OCC.Core.BRepPrimAPI import BRepPrimAPI_MakeBox
from OCC.Core.TopoDS import TopoDS_Compound
from OCC.Core.IFSelect import IFSelect_RetDone
from OCC.Core.STEPControl import STEPControl_AsIs, STEPControl_Writer
from OCC.Core.TopAbs import TopAbs_SOLID
from OCC.Core.TopExp import TopExp_Explorer

from zima_cad.model import (
    CombineMode,
    ContainerType,
    EntityKind,
    ZimaEntity,
    active_face_registry,
    create_empty_part,
)
from zima_cad.step_import import import_step_file
from zima_cad.storage import load_part_document, save_part_document


class StepImportTests(unittest.TestCase):
    def test_first_solid_mode_embeds_only_one_assembly_body(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            step_path = Path(directory) / "assembly.step"
            compound = TopoDS_Compound()
            builder = BRep_Builder()
            builder.MakeCompound(compound)
            builder.Add(compound, BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape())
            builder.Add(compound, BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape())
            writer = STEPControl_Writer()
            writer.Transfer(compound, STEPControl_AsIs)
            self.assertEqual(writer.Write(str(step_path)), IFSelect_RetDone)

            imported = import_step_file(step_path, first_solid_only=True)
            self.assertEqual(imported.solid_count, 1)
            self.assertEqual(imported.face_count, 6)

            from zima_cad.step_import import shape_from_embedded_step

            restored = shape_from_embedded_step(imported.step_data)
            explorer = TopExp_Explorer(restored, TopAbs_SOLID)
            count = 0
            while explorer.More():
                count += 1
                explorer.Next()
            self.assertEqual(count, 1)

    def test_step_geometry_is_embedded_in_part_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            directory_path = Path(directory)
            step_path = directory_path / "box.step"
            writer = STEPControl_Writer()
            writer.Transfer(
                BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape(),
                STEPControl_AsIs,
            )
            self.assertEqual(writer.Write(str(step_path)), IFSelect_RetDone)

            imported = import_step_file(step_path)
            self.assertEqual(imported.solid_count, 1)
            self.assertEqual(imported.face_count, 6)

            document = create_empty_part()
            container = document.create_container(
                "STEP", ContainerType.IMPORTED_STEP
            )
            container.add_child(ZimaEntity(
                name="box.step",
                kind=EntityKind.IMPORTED_STEP,
                combine_mode=CombineMode.ADD,
                parameters={
                    "step_data": imported.step_data,
                    "step_sha256": imported.step_sha256,
                },
            ))
            part_path = directory_path / "imported.prtz"
            save_part_document(document, part_path)
            step_path.unlink()

            loaded = load_part_document(part_path)
            shape = loaded.build_active_shape()
            explorer = TopExp_Explorer(shape, TopAbs_SOLID)
            self.assertTrue(explorer.More())
            registry = active_face_registry(loaded)
            self.assertEqual(len(registry.references), 6)
            self.assertTrue(all(
                registry.resolve(reference).shape is not None
                for reference in registry.references
            ))


if __name__ == "__main__":
    unittest.main()
