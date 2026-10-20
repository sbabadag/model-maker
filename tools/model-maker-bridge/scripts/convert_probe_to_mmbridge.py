#!/usr/bin/env python3
"""Tekla probe JSON -> .mmbridge (formatVersion 2.0) donusturucu.

Kabul edilen girdi sekilleri:
  A) GERCEK ASUS ciktisi: "schemaVersion": "bridge-geometry-probe-2"
     (component.primary / component.secondaries, child.solid.minimumPoint/maximumPoint,
      BoltArray + boltSize/boltStandard/holeTolerance/partToBeBoltedId/partToBoltToId,
      Weld + weldSizeAbove/weldMainObject/weldSecondaryObject, exportedUtc)
  B) Eski/duz fixture sekli: "probeVersion": 2, top-level primary/secondary,
     child.bbox.min/max, BoltGroup + size/standardCode/tolerance

DURUSTLUK KURALLARI (baglayici):
  * Eksik/okunamayan degerler UYDURULMAZ; unresolved diagnostic uretilir.
  * Kritik alanlar (component / en az bir ContourPlate+olcu / en az bir Bulon grubu)
    yoksa paket YAZILMAZ, stderr'e hata yazilip exit 1 donulur.
  * Bulon delik capi ve delik ekseni probe'da guvenilir degilse BOS birakilir
    (importer standard/inferred yoluyla karar verir) — burada uydurulmaz.
    Probe'un boltCoordinateSystem.axisX'i grup cercevesi eksenidir, delik ekseni
    olarak KULLANILMAZ (plaka normalinden importer cikarir).

Kullanim:
    python3 convert_probe_to_mmbridge.py <probe.json> <cikti-paket-klasoru>
Cikis: 0 = donusturuldu, 1 = kritik eksik / girdi gecersiz (paket yazilmadi).
"""

import json
import sys
from pathlib import Path

VALID_COMPONENT_TYPES = {"Connection", "Component", "Detail", "CustomPart", "Seam", "Unknown"}
# probe child type -> .mmbridge object kind (bilinmeyen -> "Unknown")
KIND_MAP = {
    "ContourPlate": "ContourPlate",
    "Beam": "Beam",
    "PolyBeam": "PolyBeam",
    "BoltGroup": "BoltGroup",
    "BoltArray": "BoltGroup",   # gercek ASUS ciktisi
    "Weld": "Weld",
    "BooleanPart": "BooleanPart",
    "Fitting": "Fitting",
    "ConstructionPlane": "ConstructionPlane",
}

IDENTITY_CS = {
    "origin": {"x": 0, "y": 0, "z": 0},
    "xAxis": {"x": 1, "y": 0, "z": 0},
    "yAxis": {"x": 0, "y": 1, "z": 0},
    "zAxis": {"x": 0, "y": 0, "z": 1},
}


def warn(msg):
    print("UYARI: " + msg, file=sys.stderr)


def as_str(v):
    return str(v)


def to_float(v, default=None):
    if isinstance(v, bool):
        return default
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str):
        s = v.strip()
        if not s:
            return default
        try:
            return float(s)
        except ValueError:
            return default
    return default


def to_int(v, default=None):
    f = to_float(v)
    if f is None:
        return default
    try:
        return int(f)
    except (ValueError, OverflowError):
        return default


def parse_metric_size(size):
    """'M20' -> 20.0, '20' -> 20.0, 20 -> 20.0; taninamazsa None."""
    if isinstance(size, (int, float)) and not isinstance(size, bool):
        return float(size) if size > 0 else None
    if not isinstance(size, str):
        return None
    digits = "".join(ch for ch in size if ch.isdigit() or ch == ".")
    if not digits:
        return None
    try:
        return float(digits)
    except ValueError:
        return None


def vec3_from(obj, diagnostics, ctx):
    """{'x','y','z'} veya [x,y,z] -> {'x','y','z'}; okunamazsa None."""
    if isinstance(obj, dict):
        x, y, z = to_float(obj.get("x")), to_float(obj.get("y")), to_float(obj.get("z"))
        if None not in (x, y, z):
            return {"x": x, "y": y, "z": z}
    elif isinstance(obj, (list, tuple)) and len(obj) == 3:
        x, y, z = to_float(obj[0]), to_float(obj[1]), to_float(obj[2])
        if None not in (x, y, z):
            return {"x": x, "y": y, "z": z}
    diagnostics["warnings"].append("%s: vec3 okunamadi: %r" % (ctx, obj))
    return None


def cross(a, b):
    return {
        "x": a["y"] * b["z"] - a["z"] * b["y"],
        "y": a["z"] * b["x"] - a["x"] * b["z"],
        "z": a["x"] * b["y"] - a["y"] * b["x"],
    }


def normalize(v):
    mag = (v["x"] ** 2 + v["y"] ** 2 + v["z"] ** 2) ** 0.5
    if mag <= 1e-12:
        return None
    return {"x": v["x"] / mag, "y": v["y"] / mag, "z": v["z"] / mag}


def component_cs(comp, diagnostics):
    """Gercek probe component.coordinateSystem (origin/axisX/axisY) -> origin/xAxis/yAxis/zAxis."""
    cs = comp.get("coordinateSystem")
    if not isinstance(cs, dict):
        return dict(IDENTITY_CS)
    origin = vec3_from(cs.get("origin"), diagnostics, "component.coordinateSystem.origin")
    ax = vec3_from(cs.get("axisX"), diagnostics, "component.coordinateSystem.axisX")
    ay = vec3_from(cs.get("axisY"), diagnostics, "component.coordinateSystem.axisY")
    if not origin or not ax or not ay:
        return dict(IDENTITY_CS)
    nx, ny = normalize(ax), normalize(ay)
    if not nx or not ny:
        return dict(IDENTITY_CS)
    nz = normalize(cross(nx, ny))
    if not nz:
        return dict(IDENTITY_CS)
    return {"origin": origin, "xAxis": nx, "yAxis": ny, "zAxis": nz}


def probe_root_version(probe):
    """Gercek 'schemaVersion' veya eski 'probeVersion' -> tanimlayici string (yoksa '')."""
    sv = probe.get("schemaVersion")
    if isinstance(sv, str) and sv:
        return sv
    pv = probe.get("probeVersion")
    if pv is not None:
        return "probeVersion:%s" % pv
    return ""


def build_manifest(probe, comp, diagnostics, root_version):
    m = {
        "formatVersion": "2.0",
        "sourceApplication": "Tekla Structures",
        "sourceVersion": as_str(probe.get("source", probe.get("sourceVersion", ""))),
        "sourceComponentType": comp.get("type", "Unknown"),
        "sourceComponentNumber": to_int(comp.get("number"), 0),
        "sourceComponentName": comp.get("name", ""),
        "sourceModelIdentifier": as_str(probe.get("sourceModelIdentifier", "")),
        "exportTimestamp": as_str(probe.get("exportedUtc", probe.get("exportTimestamp", ""))),
        "coordinateSystem": "TeklaGlobal",
        "lengthUnit": "mm",
        "componentIdentifier": "probe-" + as_str(comp.get("id", comp.get("guid", "?"))),
        "exportedObjectCount": 0,
        "knownLimitations": [],
        "exporterVersion": "2.0.0",
        "probeRootVersion": root_version,
    }
    if not comp.get("type") or comp["type"] not in VALID_COMPONENT_TYPES:
        diagnostics["unsupportedFeatures"].append(
            "component.type gecersiz/bilinmiyor: %r" % comp.get("type"))
        m["sourceComponentType"] = "Unknown"
    if not m["sourceVersion"]:
        diagnostics["warnings"].append("sourceVersion yok (probe'tan okunamadi)")
    if not m["sourceModelIdentifier"]:
        diagnostics["warnings"].append("sourceModelIdentifier yok (probe bu alani saglamadi)")
    if not m["exportTimestamp"]:
        diagnostics["warnings"].append("exportTimestamp yok (probe bu alani saglamadi)")
    return m


def role_from(member, default=""):
    role = member.get("role")
    if isinstance(role, str) and role:
        return role
    # Gercek probe: uye adi COLUMN/BEAM/... olarak geliyor.
    name = member.get("name")
    if isinstance(name, str) and name:
        up = name.upper().replace(" ", "_")
        if up in ("COLUMN", "BEAM", "BRACE", "PURLIN", "SLAB", "PANEL"):
            return up
    return default


def member_from(probe_member, diag, role_default=None, ctx="uye"):
    if not isinstance(probe_member, dict):
        return {"guid": "", "profile": "", "material": "", "role": ""}, False
    guid = as_str(probe_member.get("id", probe_member.get("guid", "")))
    profile = as_str(probe_member.get("profile", ""))
    material = as_str(probe_member.get("material", ""))
    role = role_from(probe_member, role_default or "")
    if not profile:
        diag["warnings"].append("%s %s: profile yok" % (ctx, guid))
    if not role:
        diag["warnings"].append("%s %s: role yok (bos birakildi)" % (ctx, guid))
    ref: dict[str, object] = {
        "guid": guid,
        "profile": profile,
        "material": material,
        "role": role,
    }
    iid = to_int(probe_member.get("id"))
    if iid is not None:
        ref["identifierId"] = iid
    sp = vec3_from(probe_member.get("start"), diag, ctx + " start") if "start" in probe_member else None
    ep = vec3_from(probe_member.get("end"), diag, ctx + " end") if "end" in probe_member else None
    if sp:
        ref["startPoint"] = sp
    if ep:
        ref["endPoint"] = ep
    return ref, bool(guid)


def find_members(probe, comp):
    """Gercek: comp['primary'] / comp['secondaries']; eski: probe['primary'] / probe['secondary']."""
    primary = None
    secondaries = []
    if isinstance(comp.get("primary"), dict):
        primary = comp["primary"]
    elif isinstance(probe.get("primary"), dict):
        primary = probe["primary"]
    if isinstance(comp.get("secondaries"), list):
        secondaries = comp["secondaries"]
    elif isinstance(comp.get("secondary"), list):
        secondaries = comp["secondary"]
    elif isinstance(probe.get("secondary"), list):
        secondaries = probe["secondary"]
    elif isinstance(probe.get("secondary"), dict):
        secondaries = [probe["secondary"]]
    return primary, secondaries


def build_component(probe, comp, diagnostics):
    primary_obj, secondary_objs = find_members(probe, comp)
    primary, pok = member_from(primary_obj, diagnostics, role_default="PRIMARY", ctx="primary")
    if not pok:
        diagnostics["warnings"].append("primary uye okunamadi (bos)")
    secondaries = []
    for i, s in enumerate(secondary_objs):
        if not isinstance(s, dict):
            diagnostics["warnings"].append("secondary[%d] nesne degil, atlandi" % i)
            continue
        m, _ = member_from(s, diagnostics, role_default="BEAM", ctx="secondary[%d]" % i)
        secondaries.append(m)
    return {
        "sourceType": comp.get("type", "Unknown"),
        "name": comp.get("name", ""),
        "number": to_int(comp.get("number"), 0),
        "guid": as_str(comp.get("guid", comp.get("id", ""))),
        "identifierId": to_int(comp.get("id")),
        "coordinateSystem": component_cs(comp, diagnostics),
        "primaryMember": primary,
        "secondaryMembers": secondaries,
    }


def child_bbox(child):
    """bbox.min/max (eski) veya solid.minimumPoint/maximumPoint (gercek)."""
    bbox = child.get("bbox")
    if isinstance(bbox, dict) and "min" in bbox and "max" in bbox:
        return bbox["min"], bbox["max"]
    solid = child.get("solid")
    if isinstance(solid, dict) and "minimumPoint" in solid and "maximumPoint" in solid:
        return solid["minimumPoint"], solid["maximumPoint"]
    return None, None


def child_topology(child):
    """topology.faces/... (eski) veya solid.faceCount/... (gercek)."""
    topo = child.get("topology")
    if isinstance(topo, dict):
        return {
            "faceCount": to_int(topo.get("faces"), 0),
            "loopCount": to_int(topo.get("loops"), 0),
            "faceVertexOccurrences": to_int(topo.get("faceVertexOccurrences"), 0),
            "holeLoops": to_int(topo.get("holeLoops"), 0),
        }
    solid = child.get("solid")
    if isinstance(solid, dict):
        return {
            "faceCount": to_int(solid.get("faceCount"), 0),
            "loopCount": to_int(solid.get("loopCount"), 0),
            "faceVertexOccurrences": to_int(solid.get("vertexCount"), 0),
            "holeLoops": to_int(solid.get("holeLoops"), 0),
        }
    return None


def child_to_object(child, index, root, diagnostics):
    ctype = child.get("type", "")
    o = {
        "sourceGUID": as_str(child.get("id", child.get("guid", "?"))),
        "identifierId": to_int(child.get("id")),
        "kind": "Unknown",
        "role": as_str(child.get("role", "")),
        "name": as_str(child.get("name", "")),
        "profile": as_str(child.get("profile", "")),
        "material": as_str(child.get("material", "")),
        "parentSourceGUID": as_str(child.get("parent", "")),
    }
    if ctype in KIND_MAP:
        o["kind"] = KIND_MAP[ctype]
    else:
        o["kind"] = "Unknown"
        o["unsupportedReason"] = "probe child type bilinmiyor: %r" % ctype
        diagnostics["unsupportedFeatures"].append(
            "child %s: bilinmeyen tip %r (Unsupported)" % (o["sourceGUID"], ctype))

    # bbox -> geometry dosyasi
    bmin, bmax = child_bbox(child)
    if bmin is not None and bmax is not None:
        mn = vec3_from(bmin, diagnostics, "child %s bbox.min" % o["sourceGUID"])
        mx = vec3_from(bmax, diagnostics, "child %s bbox.max" % o["sourceGUID"])
        if mn and mx:
            geo_name = "geometry/%04d.json" % index
            gdir = root / "geometry"
            gdir.mkdir(exist_ok=True)
            geo = {"formatVersion": "2.0", "type": "bbox", "bbox": {"min": mn, "max": mx}}
            (gdir / ("%04d.json" % index)).write_text(
                json.dumps(geo, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            o["geometryFile"] = geo_name

    topo = child_topology(child)
    if isinstance(topo, dict):
        o["topology"] = topo

    # Bulon grubu (BoltGroup veya BoltArray)
    if o["kind"] == "BoltGroup":
        size_raw = child.get("boltSize", child.get("size", ""))
        nominal = parse_metric_size(size_raw)
        size_str = as_str(child.get("size", ""))
        if not size_str and nominal is not None:
            size_str = "M%s" % (int(nominal) if float(nominal).is_integer() else nominal)
            diagnostics["warnings"].append(
                "bulon grubu %s: 'size' yok, boltSize=%r degerinden 'M' oneki turetildi "
                "(geometri uydurulmadi)" % (o["sourceGUID"], size_raw))
        bg = {
            "size": size_str,
            "standardCode": to_int(child.get("standardCode", child.get("boltStandard")), 0),
            "tolerance": to_float(child.get("tolerance", child.get("holeTolerance")), 0.0),
            "slottedHoleX": to_float(child.get("slottedHoleX"), 0.0),
            "slottedHoleY": to_float(child.get("slottedHoleY"), 0.0),
            "partToBeBolted": as_str(child.get("partToBeBolted", child.get("partToBeBoltedId", ""))),
            "partToBoltTo": as_str(child.get("partToBoltTo", child.get("partToBoltToId", ""))),
            "axis": as_str(child.get("axis", "")),
            "axisSource": as_str(child.get("axisSource", "")),
            "bolts": [],
        }
        if child.get("boltCoordinateSystem") is not None and not bg["axis"]:
            diagnostics["warnings"].append(
                "bulon grubu %s: probe'un boltCoordinateSystem ekseni delik ekseni olarak "
                "KULLANILMADI (grup cercevesi); importer plaka normalinden inferred karar verir"
                % o["sourceGUID"])
        positions = child.get("boltPositions", [])
        if not isinstance(positions, list):
            positions = []
            diagnostics["warnings"].append(
                "bulon grubu %s: boltPositions yok" % o["sourceGUID"])
        for p in positions:
            pv = vec3_from(p, diagnostics, "bulon %s konumu" % o["sourceGUID"])
            if not pv:
                continue
            b = {"position": pv, "holeDiameterSource": "standard"}
            if nominal is not None:
                b["nominalDiameter"] = nominal
            bg["bolts"].append(b)
        if not bg["bolts"]:
            diagnostics["warnings"].append(
                "bulon grubu %s: hic gecerli bulon konumu yok" % o["sourceGUID"])
        o["boltGroup"] = bg

    # Weld
    if o["kind"] == "Weld":
        size_above = to_float(child.get("weldSizeAbove", child.get("size")), 0.0)
        size_below = to_float(child.get("weldSizeBelow"), 0.0)
        wt = as_str(child.get("weldTypeAbove", child.get("weldType", "fillet")))
        if "FILLET" in wt.upper():
            wt = "fillet"
        o["weld"] = {
            "type": wt,
            "size": size_above or 0.0,
            "sizeBelow": size_below,
            "mainSourceGUID": as_str(child.get("weldMainObject", child.get("main", ""))),
            "secondarySourceGUID": as_str(
                child.get("weldSecondaryObject", child.get("secondary", ""))),
            "position": as_str(child.get("position", "")),
        }
        diagnostics["unsupportedFeatures"].append(
            "weld %s: yalniz metadata (model-maker kaynak motoru yok)" % o["sourceGUID"])

    return o


def collect_plate_dimensions(objects, root, diagnostics):
    """Ilk ContourPlate'in bbox boyutlarini geometry dosyasindan okur."""
    for o in objects:
        if o["kind"] == "ContourPlate" and o.get("geometryFile"):
            fp = root / o["geometryFile"]
            if fp.is_file():
                try:
                    g = json.loads(fp.read_text(encoding="utf-8"))
                    mn = g["bbox"]["min"]
                    mx = g["bbox"]["max"]
                    dx = round(mx["x"] - mn["x"], 4)
                    dy = round(mx["y"] - mn["y"], 4)
                    dz = round(mx["z"] - mn["z"], 4)
                    return dx, dy, dz
                except (OSError, ValueError, KeyError) as e:
                    diagnostics["warnings"].append("plaka bbox okunamadi: %s" % e)
            return None
    return None


def build_parameters(objects, root, diagnostics):
    params = []
    dims = collect_plate_dimensions(objects, root, diagnostics)
    if dims:
        dx, dy, dz = dims
        # kalinlik = en ince boyut (levha); diger ikisi genislik/yukseklik.
        dims_sorted = sorted([dx, dy, dz])
        thickness = dims_sorted[0]
        others = [d for d in (dx, dy, dz) if d != thickness]
        width = min(others) if others else 0.0
        height = max(others) if others else 0.0
        for name, value, ptype in (
            ("plateThickness", thickness, "Length"),
            ("plateWidth", width, "Length"),
            ("plateHeight", height, "Length"),
        ):
            params.append({"name": name, "type": ptype, "value": value, "units": "mm",
                           "source": "probe-bbox", "readable": "ok", "confidence": "observed"})

    bolt_count = 0
    bolt_size = ""
    for o in objects:
        if o["kind"] == "BoltGroup" and o.get("boltGroup"):
            bolt_count += len(o["boltGroup"]["bolts"])
            bolt_size = o["boltGroup"].get("size", "") or bolt_size
    if bolt_count:
        params.append({"name": "boltCount", "type": "Integer", "value": bolt_count,
                       "units": "", "source": "probe", "readable": "ok", "confidence": "observed"})
    if bolt_size:
        params.append({"name": "boltSize", "type": "Enum", "value": bolt_size,
                       "units": "", "source": "probe", "readable": "ok", "confidence": "observed"})
    return params


def main():
    if len(sys.argv) != 3:
        print("Kullanim: python3 convert_probe_to_mmbridge.py <probe.json> <cikti-klasoru>",
              file=sys.stderr)
        return 1
    probe_path = Path(sys.argv[1]).resolve()
    out_root = Path(sys.argv[2]).resolve()
    if not probe_path.is_file():
        print("HATA: probe dosyasi bulunamadi: %s" % probe_path, file=sys.stderr)
        return 1
    try:
        probe = json.loads(probe_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print("HATA: probe JSON okunamadi: %s" % e, file=sys.stderr)
        return 1
    if not isinstance(probe, dict):
        print("HATA: probe kok nesne olmali", file=sys.stderr)
        return 1

    root_version = probe_root_version(probe)
    if not root_version:
        print("HATA: probe kokunde 'schemaVersion'/'probeVersion' yok — paket yazilmadi",
              file=sys.stderr)
        return 1

    diagnostics = {"knownLimitations": [], "inaccessibleObjects": [],
                   "unsupportedFeatures": [], "warnings": []}
    diagnostics["knownLimitations"].append(
        "probe donusumu: kaynak geometri (weld fillet, ikincil uye govdesi) yeniden uretilmez; "
        "plaka bbox'tan eksen-hizali kutu olarak yeniden uretilir.")
    diagnostics["knownLimitations"].append(
        "probe root semasi: %s" % root_version)

    comp = probe.get("component")
    if not isinstance(comp, dict):
        print("HATA: probe.component eksik — paket yazilmadi", file=sys.stderr)
        return 1

    children = probe.get("children")
    if not isinstance(children, list):
        print("HATA: probe.children liste degil/eksik — paket yazilmadi", file=sys.stderr)
        return 1

    out_root.mkdir(parents=True, exist_ok=True)

    manifest = build_manifest(probe, comp, diagnostics, root_version)
    component = build_component(probe, comp, diagnostics)

    objects = []
    for i, child in enumerate(children):
        if not isinstance(child, dict):
            diagnostics["warnings"].append("children[%d] nesne degil, atlandi" % i)
            continue
        objects.append(child_to_object(child, i, out_root, diagnostics))
    manifest["exportedObjectCount"] = len(objects)

    # Kritik dogrulama: en az bir ContourPlate (olculu) + bir bulon grubu olmali.
    has_plate = any(o["kind"] == "ContourPlate" and o.get("geometryFile") for o in objects)
    has_bolts = any(o["kind"] == "BoltGroup" and o.get("boltGroup") for o in objects)
    if not has_plate:
        print("HATA: ContourPlate (olculu) bulunamadi — paket yazilmadi", file=sys.stderr)
        return 1
    if not has_bolts:
        print("HATA: bulon grubu (BoltGroup/BoltArray) bulunamadi — paket yazilmadi",
              file=sys.stderr)
        return 1

    parameters = build_parameters(objects, out_root, diagnostics)

    (out_root / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (out_root / "component.json").write_text(
        json.dumps(component, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (out_root / "objects.json").write_text(
        json.dumps(objects, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (out_root / "parameters.json").write_text(
        json.dumps(parameters, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (out_root / "diagnostics.json").write_text(
        json.dumps(diagnostics, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print("DONUSTURULDU: %s" % out_root)
    for w in diagnostics["warnings"]:
        print("  UYARI: " + w, file=sys.stderr)
    for u in diagnostics["unsupportedFeatures"]:
        print("  DESTEKLENMIYOR: " + u, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
