from __future__ import annotations

import base64
import gzip
import hashlib
import io
import shutil
from dataclasses import dataclass
from pathlib import Path
import tempfile
from typing import Any

from OCC.Core.IFSelect import IFSelect_RetDone
from OCC.Core.STEPControl import STEPControl_Reader
from OCC.Core.TopAbs import TopAbs_FACE, TopAbs_SOLID
from OCC.Core.TopExp import topexp
from OCC.Core.TopTools import TopTools_IndexedMapOfShape


@dataclass(frozen=True)
class StepImportResult:
    step_data: str
    step_sha256: str
    solid_count: int
    face_count: int
    shape: Any
    mesh: Any | None = None


def _subshape_count(shape, shape_type: int) -> int:
    indexed = TopTools_IndexedMapOfShape()
    topexp.MapShapes(shape, shape_type, indexed)
    return int(indexed.Size())


def import_step_file(
    file_path: Path,
    *,
    mesh_owner_id: str | None = None,
) -> StepImportResult:
    """Read a STEP document and return a self-contained compressed BREP."""
    path = Path(file_path)
    reader = STEPControl_Reader()
    status = reader.ReadFile(str(path))
    if status != IFSelect_RetDone:
        raise ValueError(f"STEP file could not be read: {path.name}")
    transferred = int(reader.TransferRoots())
    if transferred <= 0:
        raise ValueError(f"STEP file contains no transferable roots: {path.name}")
    shape = reader.OneShape()
    if shape is None or shape.IsNull():
        raise ValueError(f"STEP file contains no geometry: {path.name}")
    digest = hashlib.sha256()
    with tempfile.TemporaryFile() as compressed:
        with path.open("rb") as source, gzip.GzipFile(
            fileobj=compressed,
            mode="wb",
            compresslevel=6,
        ) as destination:
            while chunk := source.read(1024 * 1024):
                digest.update(chunk)
                destination.write(chunk)
        compressed.seek(0)
        encoded_step = base64.b64encode(compressed.read()).decode("ascii")
    solid_count = _subshape_count(shape, TopAbs_SOLID)
    face_count = _subshape_count(shape, TopAbs_FACE)
    mesh = None
    if mesh_owner_id is not None:
        # Keep OCCT translation and meshing in the same worker thread.  Some
        # large transferred Shapes block inside BRepMesh when handed to a
        # different worker after the STEP reader thread has already exited.
        from zima_cad.viewer_mesh import triangulate_shape
        mesh = triangulate_shape(
            shape,
            owner_id=mesh_owner_id,
            linear_deflection=5.0 if face_count > 5_000 else 1.0,
            angular_deflection=1.2 if face_count > 5_000 else 0.7,
            edge_linear_deflection=0.2,
            include_topology=face_count <= 5_000,
        )
    return StepImportResult(
        step_data=encoded_step,
        step_sha256=digest.hexdigest(),
        solid_count=solid_count,
        face_count=face_count,
        shape=shape,
        mesh=mesh,
    )


def shape_from_embedded_step(payload: str):
    """Rebuild an imported Shape from self-contained compressed STEP data."""
    try:
        compressed = base64.b64decode(payload, validate=True)
    except (OSError, ValueError) as error:
        raise ValueError("Embedded STEP data is damaged") from error
    with tempfile.NamedTemporaryFile(suffix=".step") as temporary:
        try:
            with gzip.GzipFile(
                fileobj=io.BytesIO(compressed), mode="rb"
            ) as source:
                shutil.copyfileobj(source, temporary, 1024 * 1024)
        except OSError as error:
            raise ValueError("Embedded STEP data is damaged") from error
        temporary.flush()
        reader = STEPControl_Reader()
        if reader.ReadFile(temporary.name) != IFSelect_RetDone:
            raise ValueError("Embedded STEP data could not be read")
        if int(reader.TransferRoots()) <= 0:
            raise ValueError("Embedded STEP data contains no transferable roots")
        shape = reader.OneShape()
    if shape is None or shape.IsNull():
        raise ValueError("Embedded STEP data contains no geometry")
    return shape
