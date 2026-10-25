#!/usr/bin/env python3
"""Read-only DSTV/NC1 versus Tekla Bridge V3 geometry comparison.

Does NOT create OCCT holes, modify a Tekla model or authorize drilling.
Missing plate identity/DSTV frame is explicitly unresolved. This script
never substitutes a bbox midpoint for a source coordinate when matching.
"""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path


BO = re.compile(
    r"^\s*([vouh])\s*([+-]?\d+(?:\.\d+)?)\s*([ous])\s*"
    r"([+-]?\d+(?:\.\d+)?)\s*(\d+(?:\.\d+)?)\s*$", re.I
)


def read_nc(path):
    data = path.read_bytes()
    text = data.decode("ascii").replace("\r\n", "\n")
    blocks = {}
    active = None
    for line in text.splitlines():
        name = line.strip()
        if name in ("ST", "AK", "IK", "BO", "EN", "KO", "PU", "SI", "KA"):
            active = name
            blocks.setdefault(name, [])
        elif active:
            blocks[active].append(line)
    if "ST" not in blocks or "BO" not in blocks or "AK" not in blocks or "EN" not in blocks:
        raise ValueError("Missing required ST/AK/BO/EN blocks")
    st = [line.strip() for line in blocks["ST"] if line.strip()]
    if "B" not in st:
        raise ValueError("NC1 not a B-profile plate")
    b = st.index("B")
    if len(st) <= b + 3:
        raise ValueError("Incomplete NC1 plate dimensions")
    length, width, thickness = map(float, st[b+1:b+4])
    holes = []
    for line in blocks["BO"]:
        if not line.strip():
            continue
        m = BO.fullmatch(line)
        if not m:
            raise ValueError("Unsupported/malformed BO record: " + line.strip())
        holes.append({"face": m[1].lower(), "u": float(m[2]),
                      "reference": m[3].lower(), "v": float(m[4]),
                      "diameter": float(m[5])})
    return {"sha256": hashlib.sha256(data).hexdigest(),
            "file": path.name, "length": length, "width": width,
            "thickness": thickness, "holes": holes,
            "profile": st[b-1], "headerFields": st[:b-1],
            "internalContourBlocks": len(blocks.get("IK", []))}


def distance3(a, b):
    return math.sqrt(sum((a[k]-b[k])**2 for k in ("x", "y", "z")))


def add_scaled(o, axis, scale):
    return {k: o[k] + axis[k] * scale for k in ("x", "y", "z")}


def nc_to_world(frame, hole):
    # Face v local BO(u,v) with real Part.GetDSTVCoordinateSystem().
    o = frame["origin"]
    p = add_scaled(o, frame["xAxis"], hole["u"])
    return add_scaled(p, frame["yAxis"], hole["v"])


def extract_bbox(package_root, plate):
    path = plate.get("geometryFile")
    if not path:
        return None
    rel = Path(path)
    if rel.is_absolute() or ".." in rel.parts:
        raise ValueError("Unsafe geometry path")
    src = package_root / rel
    data = json.loads(src.read_text(encoding="utf-8"))
    return data.get("bbox")


def check(nc_file, package_root):
    nc = read_nc(Path(nc_file))
    objects = json.loads((Path(package_root) / "objects.json").read_text(encoding="utf-8"))
    plates = [o for o in objects if o.get("kind") == "ContourPlate"]
    bgroups = [o for o in objects if o.get("kind") == "BoltGroup"]
    if len(plates) != 1 or len(bgroups) != 1:
        raise ValueError("Requires exactly one plate and one bolt group")
    plate, bolt = plates[0], bgroups[0]["boltGroup"]
    bbox = extract_bbox(Path(package_root), plate)
    if bbox is None:
        raise ValueError("Source plate bbox unavailable")

    dims = sorted(bbox["max"][k] - bbox["min"][k] for k in ("x", "y", "z"))
    dim_ok = (all(abs(a-b) < 0.01 for a, b in zip(dims,
              sorted((nc["length"], nc["width"], nc["thickness"])))) and
              plate.get("profile", "").upper() == nc["profile"].upper())
    linked_target = bolt.get("partToBoltTo") == plate.get("sourceGUID")
    measured = bolt.get("bolts", [])
    group_size = bolt.get("size", "")
    m = re.fullmatch(r"M\s*(\d+(?:\.\d+)?)", group_size, re.I)
    predicted_d = float(m[1]) + float(bolt.get("tolerance", 0.0)) if m else None
    diameter_ok = bool(predicted_d is not None and nc["holes"] and
                       all(abs(h["diameter"]-predicted_d) < 0.01
                           for h in nc["holes"]))
    count_ok = len(nc["holes"]) == len(measured)

    probe = plate.get("plateHoleProbe") or {}
    mark = (probe.get("partMark") or "").strip()
    # NC1 headers contain multiple user-configured identifiers; do not
    # choose one index without validating the template.
    mark_match = (any(field == mark for field in nc["headerFields"])
                  if mark else None)
    frame = probe.get("dstvCoordinateSystem")
    centre_match = None
    frame_bounds = None
    normal_aligns_plate_thickness = None
    global_centres = None
    if frame:
        # The NC front-face normal must follow the measured plate thickness.
        plate_spans = {k: bbox["max"][k] - bbox["min"][k]
                       for k in ("x", "y", "z")}
        thin_axis = min(plate_spans, key=plate_spans.get)
        normal = frame["zAxis"]
        normal_len = math.sqrt(sum(normal[k]**2 for k in ("x", "y", "z")))
        normal_aligns_plate_thickness = (
            normal_len > 1e-8 and
            abs(normal[thin_axis]) / normal_len >= 0.999
        )
        global_centres = [nc_to_world(frame, hole) for hole in nc["holes"]]
        # The exported BoltPositions may have a different coordinate along
        # the drill normal. Compare only the 2 dimensions transverse to the
        # NC face normal; do NOT fabricate the third dimension.
        n = frame["zAxis"]
        axis = max(("x", "y", "z"), key=lambda k: abs(n[k]))
        transverse = [k for k in ("x", "y", "z") if k != axis]
        unmatched = measured.copy()
        count_found = 0
        for p in global_centres:
            for j, b in enumerate(unmatched):
                v = b["position"]
                error = math.sqrt(sum((p[k]-v[k])**2 for k in transverse))
                if error <= 0.5:
                    count_found += 1
                    unmatched.pop(j)
                    break
        centre_match = count_found == len(nc["holes"]) == len(measured)
        frame_bounds = all(
            bbox["min"][k] - 0.5 <= p[k] <= bbox["max"][k] + 0.5
            for p in global_centres for k in ("x", "y", "z")
        )
    expected_volume = nc["length"] * nc["width"] * nc["thickness"] - sum(
        math.pi * (h["diameter"] / 2)**2 * nc["thickness"]
        for h in nc["holes"]
    )
    return {
        "nc": {k: v for k, v in nc.items() if k != "headerFields"},
        "bridgePlateGuid": plate.get("sourceGUID"),
        "bridgePlateMark": mark or None,
        "markMatchesNcHeader": mark_match,
        "partTargetGuidMatches": linked_target,
        "plateDimensionsMatch": dim_ok,
        "holeCountMatchesBoltPositions": count_ok,
        "holeDiameterMatchesNominalPlusTolerance": diameter_ok,
        "dstvFrameAvailable": bool(frame),
        "ncCentersMatchTeklaTransversePositions": centre_match,
        "ncCentersInsideMeasuredPlateBBox": frame_bounds,
        "dstvNormalAlignsPlateThickness": normal_aligns_plate_thickness,
        "ncHoleCentersGlobalOnDstvPlane": global_centres,
        "theoreticalPlateVolumeAfterSixThroughHolesMm3": round(expected_volume, 3),
        "drillingAuthorized": False,
        "reason": "NC fabrication evidence independently verified; no model mutation or automatic drill permission"
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nc1", type=Path)
    parser.add_argument("bridge_package", type=Path)
    opts = parser.parse_args()
    result = check(opts.nc1, opts.bridge_package)
    print(json.dumps(result, indent=2, ensure_ascii=False))
    if (not result["partTargetGuidMatches"] or
            not result["plateDimensionsMatch"] or
            not result["holeCountMatchesBoltPositions"] or
            not result["holeDiameterMatchesNominalPlusTolerance"] or
            result["markMatchesNcHeader"] is False or
            result["ncCentersMatchTeklaTransversePositions"] is False or
            result["ncCentersInsideMeasuredPlateBBox"] is False or
            result["dstvNormalAlignsPlateThickness"] is False):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
