from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

from OCC.Core.IFSelect import IFSelect_RetDone
from OCC.Core.STEPControl import STEPControl_AsIs, STEPControl_Writer
from OCC.Core.TopAbs import TopAbs_SOLID
from OCC.Core.TopExp import TopExp_Explorer


@dataclass(frozen=True)
class StepExportResult:
    path: Path
    solid_count: int


def export_step_shape(shape, file_path: str | Path) -> StepExportResult:
    """Atomically export a non-empty Part/Assembly result as STEP."""

    if shape is None or shape.IsNull():
        raise ValueError("The model has no result shape to export")
    solid_count = 0
    explorer = TopExp_Explorer(shape, TopAbs_SOLID)
    while explorer.More():
        solid_count += 1
        explorer.Next()
    if solid_count == 0:
        raise ValueError("The model has no solid body to export")

    target = Path(file_path)
    if target.suffix.lower() not in (".step", ".stp"):
        target = target.with_suffix(".step")
    target.parent.mkdir(parents=True, exist_ok=True)
    # Keep the STEP suffix: some OCCT builds use it while selecting the writer.
    temporary = target.with_name(f".{target.stem}.zima-export{target.suffix}")
    try:
        writer = STEPControl_Writer()
        if writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone:
            raise RuntimeError("OCCT could not transfer the model to STEP")
        if writer.Write(str(temporary)) != IFSelect_RetDone:
            raise RuntimeError("OCCT could not write the STEP file")
        os.replace(temporary, target)
    finally:
        if temporary.exists():
            temporary.unlink()
    return StepExportResult(target, solid_count)
