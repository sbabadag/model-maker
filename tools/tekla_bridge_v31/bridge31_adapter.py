#!/usr/bin/env python3
"""Read-only source adapter: Tekla Bridge legacy JSON -> existing Model Maker Bridge V3 folder.

V3.1 is a tool release, not a new reader file format. The emitted manifest
formatVersion stays "3.0" until the C++ reader gains a new format contract.
No native geometry, GUID, or missing component parameters are fabricated.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any

ADAPTER_VERSION = "3.1.0-stage1"
MAX_INPUT_BYTES = 32 * 1024 * 1024
KNOWN_KINDS = {"Beam", "PolyBeam", "ContourPlate", "BoltGroup", "Weld", "Fitting", "BooleanPart", "ControlPoint", "CutPlane", "Cut", "Part"}


def dump_json(p: Path, x: Any) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(x, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def load_json(p: Path) -> dict:
    if p.stat().st_size > MAX_INPUT_BYTES:
        raise ValueError("input exceeds 32 MiB")
    data = json.loads(p.read_text(encoding="utf-8-sig"), parse_constant=lambda c: (_ for _ in ()).throw(ValueError(f"non-finite JSON: {c}")))
    if not isinstance(data, dict):
        raise ValueError("input must be a JSON object")
    return data


def vec(x: Any) -> dict | None:
    if isinstance(x, (list, tuple)) and len(x) == 3:
        parts = x
    elif isinstance(x, dict) and all(k in x for k in ("x", "y", "z")):
        parts = [x[k] for k in ("x", "y", "z")]
    else:
        return None
    if not all(isinstance(v, (float, int)) and not isinstance(v, bool) and math.isfinite(v) for v in parts):
        return None
    return dict(zip(("x", "y", "z"), (float(p) for p in parts)))


def cross(a: dict, b: dict) -> dict:
    return {"x": a["y"] * b["z"] - a["z"] * b["y"],
            "y": a["z"] * b["x"] - a["x"] * b["z"],
            "z": a["x"] * b["y"] - a["y"] * b["x"]}


def normalize(v: dict | None) -> dict | None:
    if v is None:
        return None
    norm = math.sqrt(sum(y * y for y in v.values()))
    if not math.isfinite(norm) or norm < 1e-9:
        return None
    return {k: value / norm for k, value in v.items()}


def coord_system(source: dict | None) -> dict | None:
    if not isinstance(source, dict):
        return None
    origin = vec(source.get("origin"))
    x = normalize(vec(source.get("ax", source.get("axisX", source.get("xAxis")))))
    y = normalize(vec(source.get("ay", source.get("axisY", source.get("yAxis")))))
    if origin is None or x is None or y is None:
        return None
    z = normalize(cross(x, y))
    if z is None:
        return None
    # Do not pretend a skew source basis is an orthonormal transform.
    dot_xy = sum(x[k] * y[k] for k in ("x", "y", "z"))
    if abs(dot_xy) > 1e-4:
        return None
    return {"origin": origin, "xAxis": x, "yAxis": y, "zAxis": z}


def member(v: dict, role: str, guid_fn) -> dict:
    out = {"guid": guid_fn(v), "profile": str(v.get("profile") or ""),
           "material": str(v.get("material") or ""), "role": role,
           "roleKey": "main" if role == "MAIN" else role.lower()}
    for sk, tk in (("start", "startPoint"), ("end", "endPoint")):
        p = vec(v.get(sk))
        if p is not None:
            out[tk] = p
    return out


def cls_kind(name: str) -> str:
    return {"plate": "ContourPlate", "beam": "Beam", "bolt_group": "BoltGroup", "weld": "Weld",
            "part": "Part", "Boolean": "BooleanPart", "BoltArray": "BoltGroup"}.get(name, name)


def param_type(type_name: str, value: Any) -> str:
    key = type_name.lower()
    return {"distance": "Length", "float": "Double", "double": "Double", "angle": "Angle",
            "integer": "Integer", "int": "Integer", "option": "Integer", "bool": "Boolean", "boolean": "Boolean",
            "material": "Material", "profile": "Profile", "distance_list": "String",
            "bolt_standard": "String"}.get(key, "String" if isinstance(value, str) else "Unknown")


def logical_role(kind: str, guid: str) -> str:
    type_key = {"ContourPlate": "plate", "BoltGroup": "bolt_group", "Weld": "weld", "Fitting": "fitting",
                "Beam": "beam", "BooleanPart": "cut", "ControlPoint": "point"}.get(kind, "object")
    return type_key + "_" + hashlib.sha1(guid.encode("utf-8")).hexdigest()[:12]


def is_plate_profile(profile: str) -> bool:
    return profile.upper().startswith("PL")


def converted_type(kind: str, profile: str) -> str:
    if kind == "BoltGroup": return "BoltGroup"
    if kind == "ContourPlate" or (kind == "Beam" and is_plate_profile(profile)): return "Plate"
    if kind == "Weld": return "Weld"
    if kind in ("Fitting", "BooleanPart", "Cut", "CutPlane"): return "Cut"
    if kind in ("Beam", "PolyBeam"): return "ReferenceGeometry"
    return "Unsupported"


def make_component(source: dict, root: dict, family: str, out: Path) -> dict:
    warnings: list[str] = []
    unsupported: list[str] = []
    limitations = ["Adapter preserves source metadata, not an editable OCCT B-Rep.",
                   "Parametric equivalence and structural fabrication geometry are NOT verified."]
    if family == "v1":
        raw_child = source.get("children", [])
        raw_params = source.get("parameters", {})
        main = source.get("inputs", {}).get("primary")
        secondary = source.get("inputs", {}).get("secondary", [])
        component_kind = str(source.get("kind") or "connection")
        comp_number = source.get("number")
        comp_name = str(source.get("name") or "")
        csrc_guid = source.get("guid")
        version = str(root.get("tekla_version") or "")
        model_name = str(root.get("model") or "")
        exported = str(root.get("exported_at") or "")
        if root.get("units") != "mm":
            raise ValueError("v1 units must be mm")
        coordinates = "global"
    else:
        raw_child = source.get("children", [])
        raw_params = {}
        comp_info = source["component"]
        main = comp_info.get("primary")
        secondary = comp_info.get("secondaries") or []
        component_kind = str(comp_info.get("type") or "Connection")
        comp_number = comp_info.get("number", comp_info.get("Number"))
        comp_name = str(comp_info.get("name", comp_info.get("Name", "")))
        csrc_guid = comp_info.get("guid")
        version = str(root.get("source") or "")
        model_name = ""
        exported = str(root.get("exportedUtc") or "")
        coordinates = "global" if family == "probe2" and "global-work-plane" in str(root.get("coordinates", "")) else "unverified"
        limitations.append("Probe contains Tekla integer IDs, not persistent Tekla GUIDs. IDs are not safe for automatic cross-session binding.")
        if coordinates == "unverified":
            warnings.append("Work-plane coordinates not verified as global; manual coordinate confirmation required")

    if not isinstance(comp_number, int) or isinstance(comp_number, bool):
        try: comp_number = int(comp_number)
        except (ValueError, TypeError): raise ValueError("component number is missing")
    if not isinstance(raw_child, list): raise ValueError("children must be an array")
    if not isinstance(secondary, list): raise ValueError("secondary members must be an array")

    def key_for(v: dict) -> str:
        guid = v.get("guid")
        if isinstance(guid, str) and guid.strip() and not re.fullmatch(r"[0-]+", guid.strip()):
            return guid.strip()
        obj_id = v.get("id")
        if isinstance(obj_id, int) and obj_id > 0:
            return f"probe-id:{obj_id}"
        raise ValueError("missing source GUID and Tekla integer ID; cannot safely bind")

    source_cmp = source if family == "v1" else source["component"]
    # A sample ID is not an Open API GUID and must never be treated as verified.
    guid_pattern = re.compile(r"^\{?[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}?$")
    persistent_guids = family == "v1" and all(
        isinstance(item, dict) and isinstance(item.get("guid"), str) and
        guid_pattern.fullmatch(item["guid"])
        for item in [source_cmp, main, *secondary, *raw_child]
    )
    if family == "v1" and not persistent_guids:
        warnings.append("Some source identifiers are not canonical Tekla GUIDs; preview/test only until GUID provenance verified")
    if not csrc_guid:
        csrc_guid = key_for(source_cmp)
        warnings.append("Component uses probe-id surrogate; NOT a persistent Tekla GUID")
    if not isinstance(csrc_guid, str) or not csrc_guid.strip(): raise ValueError("invalid component GUID")

    part_guid_lookup: dict[str, str] = {}
    for part in [main, *secondary, *raw_child]:
        if isinstance(part, dict) and isinstance(part.get("id"), int) and (part.get("guid") or part.get("id")):
            part_guid_lookup[str(part["id"])] = key_for(part)

    def key_for_id(objid: Any) -> str:
        if isinstance(objid, int) and str(objid) in part_guid_lookup:
            return part_guid_lookup[str(objid)]
        if isinstance(objid, str) and objid.strip():
            return objid
        return ""

    primary = member(main, "MAIN", key_for) if isinstance(main, dict) else None
    secondaries = [member(s, f"SECONDARY_{i}", key_for) for i, s in enumerate(secondary) if isinstance(s, dict)]
    comp = {"sourceType": component_kind, "number": comp_number, "name": comp_name,
            "guid": csrc_guid, "secondaryMembers": secondaries}
    if primary is not None: comp["primaryMember"] = primary
    cs = coord_system(source_cmp.get("coordinateSystem"))
    if cs is not None: comp["coordinateSystem"] = cs
    if primary is None or not secondaries:
        warnings.append("Primary/secondary members absent; native component binding remains unresolved")

    params: list[dict] = []
    if isinstance(raw_params, dict):
        for name, x in raw_params.items():
            if not isinstance(x, dict): continue
            val = x.get("value")
            if isinstance(val, (list, dict)):
                warnings.append(f"parameter {name} complex value unsupported; null preserved")
                val = None
            typ = param_type(str(x.get("type") or ""), val)
            params.append({"name": str(name), "type": typ, "units": "mm" if typ == "Length" else "deg" if typ == "Angle" else "",
                           "value": val, "source": "Tekla Open API GetAttribute", "readable": "ok" if val is not None else "unsupported",
                           "confidence": "observed" if val is not None else "unresolved"})
    else:
        warnings.append("Legacy parameter section not an object")
    if not params:
        warnings.append("No component parameters present; no editable-param equivalence")

    objects: list[dict] = []
    operations: list[dict] = []
    relationships: list[dict] = []
    seen = set()

    for index, child in enumerate(raw_child):
        if not isinstance(child, dict): continue
        guid = key_for(child)
        if guid in seen or guid == csrc_guid: raise ValueError(f"duplicate source identifier: {guid}")
        seen.add(guid)
        raw_kind = str(child.get("type") or "")
        kind = cls_kind(raw_kind)
        profile = str(child.get("profile") or "")
        mapped_kind = converted_type(kind, profile)
        record = {"sourceGUID": guid, "identifierId": child.get("id", 0), "kind": kind,
                  "name": str(child.get("name") or ""), "profile": profile,
                  "material": str(child.get("material") or ""), "parentSourceGUID": csrc_guid,
                  "roleKey": logical_role(kind, guid), "mappedKind": mapped_kind,
                  "mapped": mapped_kind in ("Plate", "BoltGroup"),
                  "confidence": "observed" if coordinates == "global" else "unresolved",
                  "geometryRepresentation": "reference"}
        if not isinstance(record["identifierId"], int): record["identifierId"] = 0
        if mapped_kind in ("Weld", "Cut", "ReferenceGeometry", "Unsupported"):
            record["unsupportedReason"] = "not supported as editable native geometry in V3 adapter"
            unsupported.append(f"{guid}: {kind}: {record['unsupportedReason']}")
        if kind not in KNOWN_KINDS:
            unsupported.append(f"{guid}: unknown Tekla child kind {kind}")

        gc = coord_system(child.get("cs", child.get("coordinateSystem")))
        if gc: record["localToGlobal"] = gc
        geom: dict[str, Any] = {"type": "reference", "representation": "reference", "sourceKind": kind}
        solid = child.get("solid")
        if isinstance(solid, dict):
            bmin = vec(solid.get("minimumPoint"))
            bmax = vec(solid.get("maximumPoint"))
            if bmin and bmax and all(bmin[k] <= bmax[k] for k in ("x", "y", "z")):
                geom["bbox"] = {"min": bmin, "max": bmax}
            # Faces/loops are diagnostic and cannot be imported as exact OCC B-Rep.
            if "faces" in solid:
                geom["diagnosticFaces"] = solid["faces"]
                limitations.append("Face/loop vertex probes are diagnostic; exact analytic B-Rep not present")
            record["topology"] = {"faceCount": solid.get("faceCount", 0), "loopCount": solid.get("loopCount", 0),
                                   "faceVertexOccurrences": solid.get("vertexCount", 0)}
        if isinstance(child.get("contour", child.get("contourPoints")), list):
            points = child.get("contour", child.get("contourPoints"))
            geom["contourPoints"] = [v for p in points if (v := vec(p)) is not None]
            geom["reportedProfile"] = profile
            limitations.append("Contour vertices lack reliable solid thickness direction; stored as reference, not exact B-Rep")
        if isinstance(child.get("bbox_dims"), dict):
            geom["reportedDimensions"] = child["bbox_dims"]
        if "booleans" in child:
            for b_index, boolean_info in enumerate(child["booleans"]):
                btype = str(boolean_info.get("type") or "UnknownCut") if isinstance(boolean_info, dict) else str(boolean_info)
                operations.append({"id": f"{guid}:cut:{b_index}", "type": btype,
                                   "targetPartGUID": guid, "confidence": "unresolved", "inferred": False})
                unsupported.append(f"{guid}: {btype} operand and cutting geometry missing")

        if kind == "BoltGroup":
            positions = child.get("positions", child.get("boltPositions", []))
            size = child.get("size", child.get("boltSize", 0))
            std = child.get("standard", child.get("boltStandard", ""))
            tolerance = child.get("tolerance", child.get("holeTolerance", 0))
            axis_source = "unresolved"  # No reliable bolt axis in either legacy export.
            if coordinates != "global":
                warnings.append(f"{guid}: bolt coordinates unresolved (not confirmed global)")
            bolts = []
            for p in positions if isinstance(positions, list) else []:
                vp = vec(p)
                if vp is not None:
                    bolts.append({"position": vp, "nominalDiameter": float(size or 0),
                                  "holeDiameter": 0, "holeDiameterSource": "", "axisSource": axis_source})
            record["boltGroup"] = {"size": str(size), "boltStandard": str(std), "tolerance": float(tolerance or 0),
                                   "slottedHoleX": float(child.get("slottedHoleX", 0) or 0),
                                   "slottedHoleY": float(child.get("slottedHoleY", 0) or 0),
                                   "axisSource": axis_source,
                                   "partToBeBolted": key_for_id(child.get("partToBeBoltedId")),
                                   "partToBoltTo": key_for_id(child.get("partToBoltToId")), "bolts": bolts}
            for typ, ref_key in (("bolted_to", "partToBeBolted"), ("bolt_plate", "partToBoltTo")):
                if record["boltGroup"].get(ref_key):
                    relationships.append({"id": f"{guid}:{typ}", "kind": "AttachedTo", "sourceGUID": guid,
                                          "targetGUID": record["boltGroup"][ref_key], "role": typ, "confidence": "observed"})
            if not bolts:
                unsupported.append(f"{guid}: no bolt positions")
            unsupported.append(f"{guid}: bolt axis / real hole diameter not proven; do not drill holes automatically")
        if kind == "Weld":
            record["weld"] = {"type": str(child.get("type_above", child.get("weldTypeAbove", ""))),
                              "sizeAbove": float(child.get("size_above", child.get("weldSizeAbove", 0)) or 0),
                              "sizeBelow": float(child.get("size_below", child.get("weldSizeBelow", 0)) or 0),
                              "mainSourceGUID": key_for_id(child.get("main", child.get("weldMainObject"))),
                              "secondarySourceGUID": key_for_id(child.get("secondary", child.get("weldSecondaryObject")))}
            for role, source_key in (("main", "mainSourceGUID"), ("secondary", "secondarySourceGUID")):
                target = record["weld"].get(source_key)
                if target:
                    relationships.append({"id": f"{guid}:{role}", "kind": "Welds", "sourceGUID": guid,
                                          "targetGUID": target, "role": role, "confidence": "observed"})
        if kind == "Fitting":
            warnings.append(f"{guid}: fitting plane / affected member unknown; retained as unresolved object")
        if kind == "ContourPlate" or geom.get("bbox") or "reportedDimensions" in geom:
            rel = f"geometry/{index:04d}.json"
            record["geometryFile"] = rel
            dump_json(out / rel, geom)
        objects.append(record)

    if len(objects) != len(raw_child):
        warnings.append("Non-object source children were ignored")
    types = Counter(o["kind"] for o in objects)
    extra_info = {"adapterVersion": ADAPTER_VERSION, "sourceSchema": family,
                  "sourceComponentNumber": comp_number, "rawChildCount": len(raw_child),
                  "convertedChildCount": len(objects), "kindCounts": types,
                  "coordinateVerification": coordinates,
                  "sourceIdentifiersPersistent": persistent_guids}
    dump_json(out / "manifest.json", {"formatVersion": "3.0", "sourceVersion": version,
        "sourceComponentType": component_kind, "sourceComponentNumber": comp_number,
        "sourceComponentName": comp_name, "sourceModelIdentifier": "", "sourceModelName": model_name,
        "componentIdentifier": csrc_guid, "exportTimestamp": exported,
        "coordinateSystem": "global" if coordinates == "global" else "unverified",
        "lengthUnit": "mm", "exportedObjectCount": len(objects),
        "producer": {"name": "Bridge V3.1 legacy adapter", "version": ADAPTER_VERSION},
        "knownLimitations": sorted(set(limitations))})
    dump_json(out / "component.json", comp)
    dump_json(out / "parameters.json", params)
    dump_json(out / "objects.json", objects)
    dump_json(out / "diagnostics.json", {"knownLimitations": sorted(set(limitations)), "inaccessibleObjects": [],
        "unsupportedFeatures": sorted(set(unsupported)), "warnings": sorted(set(warnings)), "errors": [],
        "unresolvedObjects": [o["sourceGUID"] for o in objects if o["mappedKind"] not in ("Plate", "BoltGroup")]})
    dump_json(out / "relationships.json", relationships)
    dump_json(out / "operations.json", operations)
    dump_json(out / "provenance.json", extra_info)
    checksums = {}
    for p in sorted(out.rglob("*.json")):
        if p.name != "checksums.json":
            checksums[p.relative_to(out).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
    dump_json(out / "checksums.json", checksums)
    return {"component": comp_name, "number": comp_number, "children": len(objects),
            "params": len(params), "unsupported": len(unsupported), "warnings": len(warnings), "output": str(out)}


def convert(input_file: Path, target: Path, component_index: int | None = None) -> list[dict]:
    src = load_json(input_file)
    if src.get("schema") == "mm.tekla.v1":
        if not isinstance(src.get("components"), list):
            raise ValueError("components array required")
        components = src["components"]
        family = "v1"
    elif src.get("schemaVersion") in ("bridge-geometry-probe-2", "bridge-probe-1"):
        components = [src]
        family = "probe2" if src["schemaVersion"] == "bridge-geometry-probe-2" else "probe1"
    else:
        raise ValueError("unsupported input schema: expected mm.tekla.v1 or bridge probe 1/2")
    if not components: raise ValueError("no components to export")
    if component_index is not None:
        if component_index < 0 or component_index >= len(components): raise ValueError("component-index out of range")
        components = [components[component_index]]
    # Atomic-ish output: prevalidate source and avoid accidental overwrites.
    if target.exists(): raise FileExistsError(f"output already exists: {target}")
    planned: list[tuple[dict, Path]] = []
    for index, item in enumerate(components):
        if not isinstance(item, dict): raise ValueError("component entry must be object")
        child_dir = target if len(components) == 1 else target / f"component-{index:03d}"
        planned.append((item, child_dir))
    import tempfile
    import shutil
    with tempfile.TemporaryDirectory(prefix="bridge31-", dir=target.parent) as tmp:
        tmproot = Path(tmp) / "pack"
        result = []
        for i, (item, dest) in enumerate(planned):
            sub = tmproot if len(planned) == 1 else tmproot / f"component-{i:03d}"
            result.append(make_component(item, src, family, sub))
        shutil.move(str(tmproot), str(target))
    for r in result: r["output"] = str(target) if len(planned) == 1 else str(target / Path(r["output"]).name)
    return result


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source_json", type=Path)
    p.add_argument("--out", required=True, type=Path, help="NEW Bridge V3 package folder")
    p.add_argument("--component-index", type=int, default=None)
    args = p.parse_args()
    try:
        result = convert(args.source_json, args.out, args.component_index)
        for x in result: print(json.dumps(x, ensure_ascii=False))
        print("NOTICE: data conversion only; NOT verified native geometry or Tekla equivalence")
        return 0
    except (ValueError, FileNotFoundError, FileExistsError, OSError, TypeError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
