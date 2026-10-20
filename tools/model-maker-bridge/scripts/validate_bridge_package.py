#!/usr/bin/env python3
"""Model Maker Bridge paket dogrulayici (stdlib-only).

Yapisal (structural) dogrulama yapar; tam JSON Schema dogrulamasi DEGILDIR.
Kontrol ettikleri:
  - manifest / component / parameters / objects / diagnostics zorunlu alanlari
  - tipler ve enum degerleri
  - geometry/* dosyalarinin kendi bicimi (bbox/mesh/unsupported)
  - guvenli path (geometryFile goreli olmali, ".." ve mutlak yol yasak)
  - dosya boyutu siniri (tek dosya 32 MB, paket toplami 256 MB)

Kullanim:
    python3 validate_bridge_package.py <paket-klasoru>
Cikis: 0 = gecerli, 1 = gecersiz (nedenleri stderr'e yazar).
"""

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
    "Plate", "BoltGroup", "Cut", "Weld", "Beam", "ReferenceGeometry", "Unsupported",
}
VALID_PARAM_TYPES = {
    "Length", "Angle", "Integer", "Boolean", "Enum", "String",
    "Profile", "Material", "EntityRef", "Double", "Unknown",
}
VALID_READABLE = {"ok", "unsupported", "error"}
VALID_CONFIDENCE = {"documented", "observed", "inferred", "unresolved"}
VALID_COMPONENT_TYPES = {"Connection", "Component", "Detail", "CustomPart", "Seam", "Unknown"}
VALID_GEOM_TYPES = {"bbox", "mesh", "unsupported"}


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
    if m["formatVersion"] != "1.0":
        return fail("manifest.formatVersion 1.0 olmali, bulunan: %r" % m["formatVersion"])
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


def validate_objects(objects, root):
    if not isinstance(objects, list):
        return fail("objects liste olmali")
    guids = set()
    for i, o in enumerate(objects):
        if not isinstance(o, dict):
            return fail("objects[%d] nesne olmali" % i)
        for k in ("sourceGUID", "kind", "role"):
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
        gf = o.get("geometryFile")
        if gf is not None:
            if not safe_relative_path(gf):
                return fail("objects[%d].geometryFile guvenli goreli yol degil: %r" % (i, gf))
            fp = root / gf
            if not fp.is_file():
                return fail("objects[%d].geometryFile bulunamadi: %r" % (i, gf))
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
        if g["formatVersion"] != "1.0":
            return fail("geometry %s: formatVersion 1.0 olmali" % fp.name)
        if g["type"] not in VALID_GEOM_TYPES:
            return fail("geometry %s: type gecersiz: %r" % (fp.name, g["type"]))
        if g["type"] == "bbox":
            bb = g.get("bbox")
            if not isinstance(bb, dict) or not is_vec3(bb.get("min")) or not is_vec3(bb.get("max")):
                return fail("geometry %s: bbox.min/max gecersiz" % fp.name)
        elif g["type"] == "mesh":
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

    manifest_path = root / "manifest.json"
    if not manifest_path.is_file():
        return fail("manifest.json yok")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        return fail("manifest.json okunamadi: %s" % e)

    ok = True
    ok = validate_manifest(manifest) and ok

    # component.json
    cp = root / "component.json"
    if not cp.is_file():
        ok = fail("component.json yok") and ok
    else:
        try:
            component = json.loads(cp.read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            ok = fail("component.json okunamadi: %s" % e) and ok
        else:
            ok = validate_component(component) and ok

    # parameters.json
    pp = root / "parameters.json"
    if not pp.is_file():
        ok = fail("parameters.json yok") and ok
    else:
        try:
            params = json.loads(pp.read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            ok = fail("parameters.json okunamadi: %s" % e) and ok
        else:
            ok = validate_parameters(params) and ok

    # objects.json
    op = root / "objects.json"
    if not op.is_file():
        ok = fail("objects.json yok") and ok
    else:
        try:
            objects = json.loads(op.read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            ok = fail("objects.json okunamadi: %s" % e) and ok
        else:
            ok = validate_objects(objects, root) and ok

    # diagnostics.json
    dp = root / "diagnostics.json"
    if not dp.is_file():
        ok = fail("diagnostics.json yok") and ok
    else:
        try:
            diag = json.loads(dp.read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            ok = fail("diagnostics.json okunamadi: %s" % e) and ok
        else:
            ok = validate_diagnostics(diag) and ok

    ok = validate_geometry_files(root) and ok

    if ok:
        print("GECERLI: %s" % root)
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
