#!/usr/bin/env python3
"""Model Maker Bridge paket dogrulayici (stdlib-only).

Yapisal (structural) dogrulama yapar; tam JSON Schema dogrulamasi DEGILDIR.
Kontrol ettikleri:
  - manifest / component / parameters / objects / diagnostics zorunlu alanlari
  - tipler ve enum degerleri
  - V3: relationships.json / operations.json / checksums.json (SHA-256 dogrulamasi)
  - geometry/* dosyalarinin kendi bicimi (bbox/mesh/brep/unsupported)
  - guvenli path (geometryFile goreli olmali, ".." ve mutlak yol yasak)
  - dosya boyutu siniri (tek dosya 32 MB, paket toplami 256 MB)

Kullanim:
    python3 validate_bridge_package.py <paket-klasoru>
Cikis: 0 = gecerli, 1 = gecersiz (nedenleri stderr'e yazar).
"""

import hashlib
import json
import sys
from pathlib import Path

MAX_FILE_BYTES = 32 * 1024 * 1024
MAX_TOTAL_BYTES = 256 * 1024 * 1024

VALID_OBJECT_KINDS = {
    "Beam", "ContourPlate", "PolyBeam", "BoltGroup", "Weld",
    "BooleanPart", "Fitting", "ConstructionPlane", "Unknown",
}
VALID_MAPPED_KINDS = {
    "Plate", "BoltGroup", "Cut", "Weld", "Beam", "Fitting", "ReferenceGeometry", "Unsupported",
}
VALID_PARAM_TYPES = {
    "Length", "Angle", "Integer", "Boolean", "Enum", "String",
    "Profile", "Material", "EntityRef", "Double", "Unknown",
}
VALID_READABLE = {"ok", "unsupported", "error"}
VALID_CONFIDENCE = {"documented", "observed", "inferred", "unresolved", "unsupported"}
VALID_COMPONENT_TYPES = {"Connection", "Component", "Detail", "CustomPart", "Seam", "Unknown"}
VALID_GEOM_TYPES = {"bbox", "mesh", "brep", "unsupported"}
VALID_GEOM_REPR = {"exact", "reconstructed", "approximate", "reference", "unsupported"}
VALID_FORMAT_VERSIONS = {"1.0", "2.0", "3.0"}
VALID_RELATION_KINDS = {
    "partOf", "boltedTo", "weldedTo", "cutBy", "fittedTo", "attachedTo",
    "memberOf", "secondaryOf", "operatedBy",
}
VALID_OPERATION_TYPES = {"Cut", "Fitting", "BooleanCut", "BooleanUnion", "BoltHole"}
VALID_KEEP_SIDES = {"positive", "negative", ""}
VALID_BOOL_OP_TYPES = {"Cut", "Union", "Intersect"}


def fail(msg):
    print("HATA: " + msg, file=sys.stderr)
    return False


def is_vec3(v):
    return (isinstance(v, dict) and set(v.keys()) == {"x", "y", "z"}
            and all(isinstance(v[k], (int, float)) and not isinstance(v[k], bool) for k in ("x", "y", "z")))


def validate_manifest(m):
    req = ["formatVersion", "sourceApplication", "sourceVersion", "sourceComponentType",
           "sourceComponentNumber", "exportTimestamp", "coordinateSystem", "lengthUnit",
           "componentIdentifier"]
    for k in req:
        if k not in m:
            return fail("manifest.%s eksik" % k)
    if m["formatVersion"] not in VALID_FORMAT_VERSIONS:
        return fail("manifest.formatVersion 1.0/2.0/3.0 olmali, bulunan: %r" % m["formatVersion"])
    if m["sourceApplication"] != "Tekla Structures":
        return fail("manifest.sourceApplication 'Tekla Structures' olmali")
    if m["sourceComponentType"] not in VALID_COMPONENT_TYPES:
        return fail("manifest.sourceComponentType gecersiz: %r" % m["sourceComponentType"])
    if not isinstance(m["sourceComponentNumber"], int):
        return fail("manifest.sourceComponentNumber tamsayi olmali")
    if m["coordinateSystem"] != "TeklaGlobal":
        return fail("manifest.coordinateSystem 'TeklaGlobal' olmali")
    if m["lengthUnit"] != "mm":
        return fail("manifest.lengthUnit 'mm' olmali")
    if not isinstance(m["componentIdentifier"], str) or not m["componentIdentifier"]:
        return fail("manifest.componentIdentifier bos olmamali")
    if "producer" in m:
        p = m["producer"]
        if not isinstance(p, dict):
            return fail("manifest.producer nesne olmali")
        if "name" in p and not isinstance(p["name"], str):
            return fail("manifest.producer.name string olmali")
    return True


def validate_component(c):
    req = ["sourceType", "name", "number", "guid", "coordinateSystem",
           "primaryMember", "secondaryMembers"]
    for k in req:
        if k not in c:
            return fail("component.%s eksik" % k)
    if not isinstance(c["number"], int):
        return fail("component.number tamsayi olmali")
    cs = c["coordinateSystem"]
    for k in ("origin", "xAxis", "yAxis", "zAxis"):
        if k not in cs or not is_vec3(cs[k]):
            return fail("component.coordinateSystem.%s gecersiz" % k)
    if not isinstance(c["secondaryMembers"], list):
        return fail("component.secondaryMembers liste olmali")
    if not isinstance(c["primaryMember"], dict) or "guid" not in c["primaryMember"]:
        return fail("component.primaryMember.guid eksik")
    return True


def validate_parameters(params):
    if not isinstance(params, list):
        return fail("parameters liste olmali")
    seen = set()
    for i, p in enumerate(params):
        if not isinstance(p, dict):
            return fail("parameters[%d] nesne olmali" % i)
        for k in ("name", "type", "value", "source", "readable", "confidence"):
            if k not in p:
                return fail("parameters[%d].%s eksik" % (i, k))
        if p["type"] not in VALID_PARAM_TYPES:
            return fail("parameters[%d].type gecersiz: %r" % (i, p["type"]))
        if p["readable"] not in VALID_READABLE:
            return fail("parameters[%d].readable gecersiz: %r" % (i, p["readable"]))
        if p["confidence"] not in VALID_CONFIDENCE:
            return fail("parameters[%d].confidence gecersiz: %r" % (i, p["confidence"]))
        if p["name"] in seen:
            return fail("parameters[%d].name tekrarli: %r" % (i, p["name"]))
        seen.add(p["name"])
    return True


def safe_relative_path(p):
    if not isinstance(p, str) or not p:
        return False
    if p.startswith("/") or p.startswith("\\") or ":" in p[:3]:
        return False
    parts = p.replace("\\", "/").split("/")
    if any(part in ("..", "") for part in parts):
        return False
    return True


def validate_transform(t, ctx):
    if not isinstance(t, dict):
        return fail("%s: localToGlobal nesne olmali" % ctx)
    if "matrix" in t:
        m = t["matrix"]
        if not (isinstance(m, list) and len(m) == 4
                and all(isinstance(r, list) and len(r) == 4
                        and all(isinstance(x, (int, float)) for x in r) for r in m)):
            return fail("%s: localToGlobal.matrix 4x4 sayi olmali" % ctx)
        return True
    for k in ("origin", "xAxis", "yAxis", "zAxis"):
        if k in t and not is_vec3(t[k]):
            return fail("%s: localToGlobal.%s gecersiz" % (ctx, k))
    return True


def validate_bolt_group(bg, i):
    if not isinstance(bg, dict):
        return fail("objects[%d].boltGroup nesne olmali" % i)
    if "bolts" not in bg or not isinstance(bg["bolts"], list):
        return fail("objects[%d].boltGroup.bolts liste olmali" % i)
    for j, b in enumerate(bg["bolts"]):
        if not isinstance(b, dict) or "position" not in b or not is_vec3(b["position"]):
            return fail("objects[%d].boltGroup.bolts[%d].position gecersiz" % (i, j))
        for k in ("nominalDiameter", "holeDiameter"):
            if k in b and (not isinstance(b[k], (int, float)) or isinstance(b[k], bool)):
                return fail("objects[%d].boltGroup.bolts[%d].%s sayi olmali" % (i, j, k))
        if b.get("holeDiameterSource") not in (None, "standard", "measured", ""):
            return fail("objects[%d].boltGroup.bolts[%d].holeDiameterSource gecersiz" % (i, j))
    if bg.get("tolerance") is not None and not isinstance(bg["tolerance"], (int, float)):
        return fail("objects[%d].boltGroup.tolerance sayi olmali" % i)
    if bg.get("direction") is not None and not is_vec3(bg["direction"]):
        return fail("objects[%d].boltGroup.direction gecersiz" % i)
    return True


def validate_weld(w, i):
    if not isinstance(w, dict):
        return fail("objects[%d].weld nesne olmali" % i)
    for k in ("size", "sizeAbove", "sizeBelow"):
        if k in w and (not isinstance(w[k], (int, float)) or isinstance(w[k], bool)):
            return fail("objects[%d].weld.%s sayi olmali" % (i, k))
    return True


def validate_fitting(f, i):
    if not isinstance(f, dict):
        return fail("objects[%d].fitting nesne olmali" % i)
    if "plane" in f and not isinstance(f["plane"], dict):
        return fail("objects[%d].fitting.plane nesne olmali" % i)
    if "normal" in f and f["normal"] is not None and not is_vec3(f["normal"]):
        return fail("objects[%d].fitting.normal gecersiz" % i)
    if f.get("keepSide") not in (None, "positive", "negative", ""):
        return fail("objects[%d].fitting.keepSide gecersiz" % i)
    return True


def validate_boolean_part(b, i):
    if not isinstance(b, dict):
        return fail("objects[%d].booleanPart nesne olmali" % i)
    if "operationType" in b and b["operationType"] not in VALID_BOOL_OP_TYPES:
        return fail("objects[%d].booleanPart.operationType gecersiz" % i)
    return True


def validate_topology(t, i):
    if not isinstance(t, dict):
        return fail("objects[%d].topology nesne olmali" % i)
    for k in ("faceCount", "loopCount", "faceVertexOccurrences", "holeLoops"):
        if k in t and not isinstance(t[k], int):
            return fail("objects[%d].topology.%s tamsayi olmali" % (i, k))
    return True


def validate_objects(objects, root):
    if not isinstance(objects, list):
        return fail("objects liste olmali")
    guids = set()
    for i, o in enumerate(objects):
        if not isinstance(o, dict):
            return fail("objects[%d] nesne olmali" % i)
        # V2: role zorunlu; V3: roleKey zorunlu. En az biri olmali.
        if "role" not in o and "roleKey" not in o:
            return fail("objects[%d].role/roleKey eksik (V2 'role', V3 'roleKey')" % i)
        for k in ("sourceGUID", "kind"):
            if k not in o:
                return fail("objects[%d].%s eksik" % (i, k))
        if o["sourceGUID"] in guids:
            return fail("objects[%d].sourceGUID tekrarli: %r" % (i, o["sourceGUID"]))
        guids.add(o["sourceGUID"])
        if o["kind"] not in VALID_OBJECT_KINDS:
            return fail("objects[%d].kind gecersiz: %r" % (i, o["kind"]))
        if "mapped" in o and not isinstance(o["mapped"], bool):
            return fail("objects[%d].mapped bool olmali" % i)
        if "mappedKind" in o and o["mappedKind"] not in VALID_MAPPED_KINDS:
            return fail("objects[%d].mappedKind gecersiz: %r" % (i, o.get("mappedKind")))
        if "geometryRepresentation" in o and o["geometryRepresentation"] not in VALID_GEOM_REPR:
            return fail("objects[%d].geometryRepresentation gecersiz: %r" % (i, o.get("geometryRepresentation")))
        if "confidence" in o and o["confidence"] not in VALID_CONFIDENCE:
            return fail("objects[%d].confidence gecersiz: %r" % (i, o.get("confidence")))
        if "localToGlobal" in o and not validate_transform(o["localToGlobal"], "objects[%d]" % i):
            return False
        gf = o.get("geometryFile")
        if gf is not None:
            if not safe_relative_path(gf):
                return fail("objects[%d].geometryFile guvenli goreli yol degil: %r" % (i, gf))
            fp = root / gf
            if not fp.is_file():
                return fail("objects[%d].geometryFile bulunamadi: %r" % (i, gf))
        if "boltGroup" in o and not validate_bolt_group(o["boltGroup"], i):
            return False
        if "weld" in o and not validate_weld(o["weld"], i):
            return False
        if "fitting" in o and not validate_fitting(o["fitting"], i):
            return False
        if "booleanPart" in o and not validate_boolean_part(o["booleanPart"], i):
            return False
        if "topology" in o and not validate_topology(o["topology"], i):
            return False
    return True


def validate_relationships(rels):
    if rels is None:
        return True
    if not isinstance(rels, list):
        return fail("relationships liste olmali")
    for i, r in enumerate(rels):
        if not isinstance(r, dict):
            return fail("relationships[%d] nesne olmali" % i)
        for k in ("kind", "sourceGUID", "targetGUID"):
            if k not in r:
                return fail("relationships[%d].%s eksik" % (i, k))
        if r["kind"] not in VALID_RELATION_KINDS:
            return fail("relationships[%d].kind gecersiz: %r" % (i, r["kind"]))
        if r.get("confidence") not in (None, "documented", "observed", "inferred", "unresolved", "unsupported"):
            return fail("relationships[%d].confidence gecersiz" % i)
    return True


def validate_operations(ops, root):
    if ops is None:
        return True
    if not isinstance(ops, list):
        return fail("operations liste olmali")
    ids = set()
    for i, op in enumerate(ops):
        if not isinstance(op, dict):
            return fail("operations[%d] nesne olmali" % i)
        for k in ("id", "type", "targetPartGUID"):
            if k not in op:
                return fail("operations[%d].%s eksik" % (i, k))
        if op["type"] not in VALID_OPERATION_TYPES:
            return fail("operations[%d].type gecersiz: %r" % (i, op["type"]))
        if op["id"] in ids:
            return fail("operations[%d].id tekrarli: %r" % (i, op["id"]))
        ids.add(op["id"])
        if op.get("keepSide") not in (None, "positive", "negative", ""):
            return fail("operations[%d].keepSide gecersiz" % i)
        if "fittingPlane" in op:
            fp = op["fittingPlane"]
            if not isinstance(fp, dict) or "origin" not in fp or "normal" not in fp \
                    or not is_vec3(fp["origin"]) or not is_vec3(fp["normal"]):
                return fail("operations[%d].fittingPlane gecersiz" % i)
        fsf = op.get("finalSolidGeometryFile")
        if fsf is not None:
            if not safe_relative_path(fsf):
                return fail("operations[%d].finalSolidGeometryFile guvenli degil" % i)
            if not (root / fsf).is_file():
                return fail("operations[%d].finalSolidGeometryFile bulunamadi: %r" % (i, fsf))
    return True


def validate_checksums(checksums, root):
    if checksums is None:
        return True
    if not isinstance(checksums, dict):
        return fail("checksums nesne olmali")
    for rel, want in checksums.items():
        if not isinstance(rel, str) or not safe_relative_path(rel):
            return fail("checksums anahtari guvenli goreli yol olmali: %r" % rel)
        if not isinstance(want, str) or len(want) != 64:
            return fail("checksums[%s] 64 haneli SHA-256 hex olmali" % rel)
        fp = root / rel
        if not fp.is_file():
            return fail("checksums[%s] dosya bulunamadi" % rel)
        got = hashlib.sha256(fp.read_bytes()).hexdigest()
        if got.lower() != want.lower():
            return fail("checksums[%s] uyusmuyor (butunluk hatasi)" % rel)
    return True


def validate_geometry_files(root):
    gdir = root / "geometry"
    if not gdir.is_dir():
        return True  # geometry klasoru opsiyonel
    for fp in gdir.iterdir():
        if not fp.is_file() or fp.suffix != ".json":
            continue
        if fp.stat().st_size > MAX_FILE_BYTES:
            return fail("geometry dosyasi cok buyuk: %s" % fp.name)
        try:
            g = json.loads(fp.read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            return fail("geometry okunamadi %s: %s" % (fp.name, e))
        if not isinstance(g, dict) or "formatVersion" not in g or "type" not in g:
            return fail("geometry %s: formatVersion/type eksik" % fp.name)
        if g["formatVersion"] not in VALID_FORMAT_VERSIONS:
            return fail("geometry %s: formatVersion 1.0/2.0/3.0 olmali" % fp.name)
        if g["type"] not in VALID_GEOM_TYPES:
            return fail("geometry %s: type gecersiz: %r" % (fp.name, g["type"]))
        if "representation" in g and g["representation"] not in VALID_GEOM_REPR:
            return fail("geometry %s: representation gecersiz: %r" % (fp.name, g["representation"]))
        if g["type"] == "bbox":
            bb = g.get("bbox")
            if not isinstance(bb, dict) or not is_vec3(bb.get("min")) or not is_vec3(bb.get("max")):
                return fail("geometry %s: bbox.min/max gecersiz" % fp.name)
        elif g["type"] in ("mesh", "brep"):
            m = g.get("mesh")
            if not isinstance(m, dict) or "vertices" not in m or "triangles" not in m:
                return fail("geometry %s: mesh.vertices/triangles eksik" % fp.name)
            v = m["vertices"]
            t = m["triangles"]
            if not isinstance(v, list) or not all(isinstance(x, list) and len(x) == 3 for x in v):
                return fail("geometry %s: vertices [[x,y,z],...] olmali" % fp.name)
            if not isinstance(t, list):
                return fail("geometry %s: triangles liste olmali" % fp.name)
            for tri in t:
                if not (isinstance(tri, list) and len(tri) == 3 and all(isinstance(k, int) and 0 <= k < len(v) for k in tri)):
                    return fail("geometry %s: triangles [[i,j,k],...] indeksleri gecersiz" % fp.name)
    return True


def validate_diagnostics(d):
    for k in ("knownLimitations", "inaccessibleObjects", "unsupportedFeatures", "warnings"):
        if k not in d or not isinstance(d[k], list):
            return fail("diagnostics.%s liste olmali" % k)
    return True


def read_json(root, name):
    fp = root / name
    if not fp.is_file():
        return None
    try:
        return json.loads(fp.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def main():
    if len(sys.argv) != 2:
        print("Kullanim: python3 validate_bridge_package.py <paket-klasoru>", file=sys.stderr)
        return 1
    root = Path(sys.argv[1]).resolve()
    if not root.is_dir():
        return fail("paket klasoru bulunamadi: %s" % root)

    # toplam boyut siniri
    total = sum(fp.stat().st_size for fp in root.rglob("*") if fp.is_file())
    if total > MAX_TOTAL_BYTES:
        return fail("paket toplam boyutu %d bayt, sinir %d" % (total, MAX_TOTAL_BYTES))

    manifest = read_json(root, "manifest.json")
    if manifest is None:
        return fail("manifest.json yok/okunamadi")

    ok = True
    ok = validate_manifest(manifest) and ok

    component = read_json(root, "component.json")
    if component is None:
        ok = fail("component.json yok/okunamadi") and ok
    else:
        ok = validate_component(component) and ok

    params = read_json(root, "parameters.json")
    if params is None:
        ok = fail("parameters.json yok/okunamadi") and ok
    else:
        ok = validate_parameters(params) and ok

    objects = read_json(root, "objects.json")
    if objects is None:
        ok = fail("objects.json yok/okunamadi") and ok
    else:
        ok = validate_objects(objects, root) and ok

    diag = read_json(root, "diagnostics.json")
    if diag is None:
        ok = fail("diagnostics.json yok/okunamadi") and ok
    else:
        ok = validate_diagnostics(diag) and ok

    # V3 opsiyonel dosyalar (varlarsa dogrulanir).
    relationships = read_json(root, "relationships.json")
    if relationships is not None:
        ok = validate_relationships(relationships) and ok

    operations = read_json(root, "operations.json")
    if operations is not None:
        ok = validate_operations(operations, root) and ok

    checksums = read_json(root, "checksums.json")
    if checksums is not None:
        ok = validate_checksums(checksums, root) and ok

    ok = validate_geometry_files(root) and ok

    if ok:
        print("GECERLI: %s" % root)
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
