"""Prepare explicit factory authoring inputs for native symbol-anchor refresh.

This is an offline authoring step, never a document-reader compatibility path.
The changed native tool must calculate, save and reopen each prepared asset.
"""
import argparse
import json
from pathlib import Path


def prepare(source: Path, target: Path) -> int:
    changed = 0
    lines = source.read_text(encoding="utf-8-sig").splitlines(keepends=True)
    for index, line in enumerate(lines):
        key, separator, payload = line.partition("=")
        if not separator or key.strip().lower() != "data" or not payload.lstrip().startswith("{"):
            continue
        data = json.loads(payload)
        for symbol in data.get("symbols", []):
            anchor = symbol.get("anchor_point_id")
            if anchor:
                continue
            anchor = symbol["id"] + ":anchor"
            if any(point["id"] == anchor for point in data["points"]):
                raise ValueError(f"Anchor identity collision in {source}: {anchor}")
            symbol["anchor_point_id"] = anchor
            data["points"].append({"id": anchor, "x": symbol["x"], "y": symbol["y"],
                                   "fixed": False, "construction": True,
                                   "centerline": False, "visible_in_3d": True})
            changed += 1
        lines[index] = key + "=" + json.dumps(data, ensure_ascii=False, separators=(",", ":")) + "\n"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("".join(lines), encoding="utf-8")
    return changed


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    args = parser.parse_args()
    print(json.dumps({"source": str(args.source), "prepared": str(args.target),
                      "anchors": prepare(args.source, args.target)}))
