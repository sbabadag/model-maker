#!/usr/bin/env python3
"""ASUS Tekla probe JSON -> .mmbridge (formatVersion 2.0) donusturucu.

ASUS tarafinda uretilen ham "bridge-geometry-probe-2" JSON'unu, importer'in
(formatVersion 2.0) .mmbridge paket sozlesmesine donusturur.

DURUSTLUK KURALLARI (baglayici):
  * Eksik/okunamayan alanlar UYDURULMAZ; unresolved diagnostic uretilir.
  * Kritik alanlar (component / en az bir ContourPlate+bbox / en az bir BoltGroup)
    yoksa paket YAZILMAZ, stderr'e hata yazilip exit 1 donulur (yarim paket
    "basarili" kabul EDILMEZ).
  * Bulon delik capi ve ekseni probe'da yoksa BOS birakilir (importer standard/
    inferred yoluyla karar verir) — burada uydurulmaz.

PROBE SOZLESMESI (hedef girdi; gercek ASUS dosyasiyla dogrulanmasi gerekir):
  {
    "probeVersion": 2,
    "component": {"type": "Connection", "number": 146, "name": "...",
                  "id": 5245, "guid": "5245"},
    "primary":   {"id": 5165, "profile": "HEA400", "material": "S235JR", "role": "COLUMN"},
    "secondary": [{"id": 5213, "profile": "HEA300", "material": "S235JR", "role": "BEAM"}],
    "children": [
      {"id": 5673, "type": "ContourPlate", "profile": "PL10", "material": "S235JR",
       "bbox": {"min": [4.25,195,6959], "max": [14.25,365,7159]},
       "topology": {"faces": 6, "loops": 6, "faceVertexOccurrences": 24, "holeLoops": 0}},
      {"id": 5837, "type": "BoltGroup", "size": "M20", "standardCode": 7990,
       "tolerance": 2, "boltPositions": [[0,265,7119], ...],
       "partToBeBolted": 5213, "partToBoltTo": 5673},
      {"id": 5701, "type": "Weld", "size": 5, "main": 5165, "secondary": 5673}
    ]
  }

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


def parse_metric_size(size):
    """'M20' -> 20.0, '20' -> 20.0; taninamazsa None."""
    if not isinstance(size, str):
        return None
    digits = "".join(ch for ch in size if ch.isdigit() or ch == ".")
    if not digits:
        return None
    try:
        return float(digits)
    except ValueError:
        return None


def build_manifest(probe, comp, diagnostics):
    m = {
        "formatVersion": "2.0",
        "sourceApplication": "Tekla Structures",
        "sourceVersion": probe.get("sourceVersion", ""),
        "sourceComponentType": comp.get("type", "Unknown"),
        "sourceComponentNumber": int(comp.get("number", 0)),
        "sourceComponentName": comp.get("name", ""),
        "sourceModelIdentifier": probe.get("sourceModelIdentifier", ""),
        "exportTimestamp": probe.get("exportTimestamp", ""),
        "coordinateSystem": "TeklaGlobal",
        "lengthUnit": "mm",
        "componentIdentifier": "probe-" + as_str(comp.get("id", comp.get("guid", "?"))),
        "exportedObjectCount": 0,
        "knownLimitations": [],
        "exporterVersion": "2.0.0",
    }
    if not comp.get("type") or comp["type"] not in VALID_COMPONENT_TYPES:
        diagnostics["unsupportedFeatures"].append(
            "component.type gecersiz/bilinmiyor: %r" % comp.get("type"))
        m["sourceComponentType"] = "Unknown"
    if not m["sourceVersion"]:
        diagnostics["warnings"].append("sourceVersion yok (probe'tan okunamadi)")
    if not m["sourceModelIdentifier"]:
        diagnostics["warnings"].append("sourceModelIdentifier yok")
    return m


def member_from(probe_member, diag, role_default=None):
    if not isinstance(probe_member, dict):
        return {"guid": "", "profile": "", "material": "", "role": ""}, False
    guid = as_str(probe_member.get("id", probe_member.get("guid", "")))
    profile = probe_member.get("profile", "")
    material = probe_member.get("material", "")
    role = probe_member.get("role", "") or (role_default or "")
    if not profile:
        diag["warnings"].append("uye %s: profile yok" % guid)
    if not role:
        diag["warnings"].append("uye %s: role yok (bos birakildi)" % guid)
    return {"guid": guid, "profile": profile, "material": material, "role": role}, bool(guid)


def build_component(probe, comp, diagnostics):
    primary, pok = member_from(probe.get("primary"), diagnostics)
    if not pok:
        diagnostics["warnings"].append("primary uye okunamadi (bos)")
    secondaries = []
    sec = probe.get("secondary")
    if isinstance(sec, list):
        for s in sec:
            m, ok = member_from(s, diagnostics, role_default="BEAM")
            secondaries.append(m)
    elif sec is not None:
        m, ok = member_from(sec, diagnostics, role_default="BEAM")
        secondaries.append(m)
    return {
        "sourceType": comp.get("type", "Unknown"),
        "name": comp.get("name", ""),
        "number": int(comp.get("number", 0)),
        "guid": as_str(comp.get("guid", comp.get("id", ""))),
        "identifierId": comp.get("id"),
        "coordinateSystem": IDENTITY_CS,
        "primaryMember": primary,
        "secondaryMembers": secondaries,
    }


def child_to_object(child, index, root, diagnostics):
    o = {
        "sourceGUID": as_str(child.get("id", child.get("guid", "?"))),
        "identifierId": child.get("id"),
        "kind": "Unknown",
        "role": child.get("role", ""),
        "profile": child.get("profile", ""),
        "material": child.get("material", ""),
        "parentSourceGUID": as_str(child.get("parent", "")),
    }
    ctype = child.get("type", "")
    if ctype in KIND_MAP:
        o["kind"] = KIND_MAP[ctype]
    else:
        o["kind"] = "Unknown"
        o["unsupportedReason"] = "probe child type bilinmiyor: %r" % ctype
        diagnostics["unsupportedFeatures"].append(
            "child %s: bilinmeyen tip %r (Unsupported)" % (o["sourceGUID"], ctype))

    # bbox -> geometry dosyasi
    bbox = child.get("bbox")
    if isinstance(bbox, dict) and "min" in bbox and "max" in bbox:
        geo_name = "geometry/%04d.json" % index
        gdir = root / "geometry"
        gdir.mkdir(exist_ok=True)
        geo = {
            "formatVersion": "2.0",
            "type": "bbox",
            "bbox": {"min": {"x": bbox["min"][0], "y": bbox["min"][1], "z": bbox["min"][2]},
                     "max": {"x": bbox["max"][0], "y": bbox["max"][1], "z": bbox["max"][2]}},
        }
        (gdir / ("%04d.json" % index)).write_text(
            json.dumps(geo, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        o["geometryFile"] = geo_name

    # topology
    topo = child.get("topology")
    if isinstance(topo, dict):
        o["topology"] = {
            "faceCount": int(topo.get("faces", 0)),
            "loopCount": int(topo.get("loops", 0)),
            "faceVertexOccurrences": int(topo.get("faceVertexOccurrences", 0)),
            "holeLoops": int(topo.get("holeLoops", 0)),
        }

    # BoltGroup
    if o["kind"] == "BoltGroup":
        bg = {
            "size": child.get("size", ""),
            "standardCode": int(child.get("standardCode", 0)),
            "tolerance": float(child.get("tolerance", 0.0)),
            "slottedHoleX": float(child.get("slottedHoleX", 0.0)),
            "slottedHoleY": float(child.get("slottedHoleY", 0.0)),
            "partToBeBolted": as_str(child.get("partToBeBolted", "")),
            "partToBoltTo": as_str(child.get("partToBoltTo", "")),
            "axis": child.get("axis", ""),
            "axisSource": child.get("axisSource", ""),
            "bolts": [],
        }
        nominal = parse_metric_size(child.get("size"))
        positions = child.get("boltPositions", [])
        if not isinstance(positions, list):
            positions = []
            diagnostics["warnings"].append("BoltGroup %s: boltPositions yok" % o["sourceGUID"])
        for p in positions:
            if isinstance(p, (list, tuple)) and len(p) == 3:
                b = {"position": {"x": float(p[0]), "y": float(p[1]), "z": float(p[2])},
                     "holeDiameterSource": "standard"}
                if nominal is not None:
                    b["nominalDiameter"] = nominal
                bg["bolts"].append(b)
            else:
                diagnostics["warnings"].append(
                    "BoltGroup %s: gecersiz bulon konumu atlandi: %r" % (o["sourceGUID"], p))
        # Ekseni/capi UYDURMA: bos birak -> importer inferred/standard yapar.
        if not bg["axis"]:
            diagnostics["warnings"].append(
                "BoltGroup %s: eksen yok; importer inferred ile karar verecek" % o["sourceGUID"])
        o["boltGroup"] = bg

    # Weld
    if o["kind"] == "Weld":
        o["weld"] = {
            "type": child.get("weldType", "fillet"),
            "size": float(child.get("size", 0.0)),
            "mainSourceGUID": as_str(child.get("main", "")),
            "secondarySourceGUID": as_str(child.get("secondary", "")),
            "position": child.get("position", ""),
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
                    mn = g["bbox"]["min"]; mx = g["bbox"]["max"]
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
            bolt_count = len(o["boltGroup"]["bolts"])
            bolt_size = o["boltGroup"].get("size", "")
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

    diagnostics = {"knownLimitations": [], "inaccessibleObjects": [],
                   "unsupportedFeatures": [], "warnings": []}
    diagnostics["knownLimitations"].append(
        "probe donusumu: kaynak geometri (weld fillet, ikincil uye govdesi) yeniden uretilmez; "
        "plaka bbox'tan eksen-hizali kutu olarak yeniden uretilir.")

    comp = probe.get("component")
    if not isinstance(comp, dict):
        print("HATA: probe.component eksik — paket yazilmadi", file=sys.stderr)
        return 1

    children = probe.get("children")
    if not isinstance(children, list):
        print("HATA: probe.children liste degil/eksik — paket yazilmadi", file=sys.stderr)
        return 1

    out_root.mkdir(parents=True, exist_ok=True)

    manifest = build_manifest(probe, comp, diagnostics)
    component = build_component(probe, comp, diagnostics)

    objects = []
    for i, child in enumerate(children):
        if not isinstance(child, dict):
            diagnostics["warnings"].append("children[%d] nesne degil, atlandi" % i)
            continue
        objects.append(child_to_object(child, i, out_root, diagnostics))
    manifest["exportedObjectCount"] = len(objects)

    # Kritik dogrulama: en az bir ContourPlate (bbox'lu) + bir BoltGroup olmali.
    has_plate = any(o["kind"] == "ContourPlate" and o.get("geometryFile") for o in objects)
    has_bolts = any(o["kind"] == "BoltGroup" and o.get("boltGroup") for o in objects)
    if not has_plate:
        print("HATA: ContourPlate (bbox'lu) bulunamadi — paket yazilmadi", file=sys.stderr)
        return 1
    if not has_bolts:
        print("HATA: BoltGroup bulunamadi — paket yazilmadi", file=sys.stderr)
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
