#!/usr/bin/env python3
"""V3 fixture uretici (deterministik; stdlib-only).

Uc V3 .mmbridge paketi uretir:
  1. fixture-146-shear-plate-v3.mmbridge  — 146 Shear plate (bbox + 6 bolt + weld)
  2. fixture-144-end-plate-v3.mmbridge     — 144 End plate (cok parcali: end plate +
                                            2 stiffener + 2 bolt grubu + 2 weld +
                                            fitting; mesh representation)
  3. fixture-mesh-box-v3.mmbridge          — tek mesh kutu (mesh->solid yolu)

Her paket icin checksums.json (goreli yol -> SHA-256) hesaplanir.

Kullanim:
    python3 make_fixtures_v3.py [cikti-koku]
Varsayilan cikti: tools/model-maker-bridge/fixtures/
"""

import hashlib
import json
import os
import sys

FIXTURES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "fixtures")


def vec(x, y, z):
    return {"x": x, "y": y, "z": z}


def box_mesh(mn, mx):
    """Eksen-hizali kutu icin disa-donuk CCW ucgen mesh (8 kose, 12 ucgen)."""
    x0, y0, z0 = mn
    x1, y1, z1 = mx
    v = [
        [x0, y0, z0],  # 0
        [x1, y0, z0],  # 1
        [x1, y1, z0],  # 2
        [x0, y1, z0],  # 3
        [x0, y0, z1],  # 4
        [x1, y0, z1],  # 5
        [x1, y1, z1],  # 6
        [x0, y1, z1],  # 7
    ]
    t = [
        [4, 5, 6], [4, 6, 7],  # +z
        [0, 2, 1], [0, 3, 2],  # -z
        [2, 3, 6], [3, 7, 6],  # +y
        [0, 5, 4], [0, 1, 5],  # -y
        [1, 2, 6], [1, 6, 5],  # +x
        [0, 7, 3], [0, 4, 7],  # -x
    ]
    return {"vertices": v, "triangles": t}


def write_json(root, rel, obj):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2)
        f.write("\n")


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def finalize(root, rels):
    """checksums.json uret (goreli yol -> SHA-256)."""
    checksums = {}
    for rel in rels:
        checksums[rel.replace("\\", "/")] = sha256(os.path.join(root, rel))
    write_json(root, "checksums.json", checksums)


def cs():
    return {
        "origin": vec(0, 0, 0),
        "xAxis": vec(1, 0, 0),
        "yAxis": vec(0, 1, 0),
        "zAxis": vec(0, 0, 1),
    }


def base_manifest(version, num, name, ident, extra=None):
    m = {
        "formatVersion": version,
        "sourceApplication": "Tekla Structures",
        "sourceVersion": "2025 SP6",
        "producer": {"name": "ModelMaker.TeklaBridge", "version": "3.0.0"},
        "sourceComponentType": "Connection",
        "sourceComponentNumber": num,
        "sourceComponentName": name,
        "sourceModelIdentifier": "FIXTURE-V3-" + ident,
        "sourceModelName": "fixture.db1",
        "exportTimestamp": "2026-10-21T00:00:00Z",
        "coordinateSystem": "TeklaGlobal",
        "lengthUnit": "mm",
        "componentIdentifier": "FIXTURE-V3-" + ident,
        "knownLimitations": [
            "FIXTURE-V3: elle yazilmis test verisi; gercek Tekla extractor ciktisi degil."
        ],
        "exporterVersion": "3.0.0",
    }
    if extra:
        m.update(extra)
    return m


def component(num, name, guid, primary, secondaries):
    return {
        "sourceType": "Connection",
        "name": name,
        "number": num,
        "guid": guid,
        "identifierId": int(guid),
        "coordinateSystem": cs(),
        "primaryMember": primary,
        "secondaryMembers": secondaries,
    }


def member(guid, profile, material, role, start=None, end=None):
    m = {"guid": guid, "profile": profile, "material": material,
         "role": role, "roleKey": role}
    if start is not None:
        m["startPoint"] = start
    if end is not None:
        m["endPoint"] = end
    return m


def make_146(root):
    plate_guid = "5673"
    bolt_guid = "5837"
    weld_guid = "5701"
    comp_guid = "5245"

    manifest = base_manifest("3.0", 146, "Shear plate simple", "146-5245")
    manifest["exportedObjectCount"] = 3

    comp = component(146, "Shear plate simple", comp_guid,
                     member("5165", "HEA400", "S235JR", "COLUMN"),
                     [member("5213", "HEA300", "S235JR", "BEAM",
                             vec(0, 195, 7119), vec(6000, 195, 7119))])

    plate_bbox = {"min": vec(4.25, 195, 6959), "max": vec(14.25, 365, 7159)}

    objects = [
        {
            "sourceGUID": plate_guid, "identifierId": 5673,
            "kind": "ContourPlate", "name": "PLATE",
            "roleKey": "shear_tab", "profile": "PL10", "material": "S235JR",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "Plate",
            "geometryFile": "geometry/0000.json",
            "geometryRepresentation": "reconstructed", "confidence": "observed",
            "topology": {"faceCount": 6, "loopCount": 6,
                         "faceVertexOccurrences": 24, "holeLoops": 0},
        },
        {
            "sourceGUID": bolt_guid, "identifierId": 5837,
            "kind": "BoltGroup", "name": "BOLT",
            "roleKey": "beam_bolts",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "BoltGroup",
            "boltGroup": {
                "size": "M20", "standardCode": 7990, "tolerance": 2.0,
                "slottedHoleX": 0.0, "slottedHoleY": 0.0,
                "partToBeBolted": "5213", "partToBoltTo": plate_guid,
                "axis": "x", "axisSource": "inferred",
                "boltType": "BOLT", "boltStandard": "7990",
                "direction": vec(1, 0, 0),
                "bolts": [
                    {"position": vec(0, 265, 7119), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                    {"position": vec(0, 325, 7119), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                    {"position": vec(0, 265, 7059), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                    {"position": vec(0, 325, 7059), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                    {"position": vec(0, 265, 6999), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                    {"position": vec(0, 325, 6999), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                ],
            },
        },
        {
            "sourceGUID": weld_guid, "identifierId": 5701,
            "kind": "Weld", "name": "WELD",
            "roleKey": "shear_tab_weld",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "Weld",
            "weld": {
                "type": "fillet", "size": 5.0, "sizeAbove": 5.0, "sizeBelow": 5.0,
                "mainSourceGUID": "5165", "secondarySourceGUID": plate_guid,
                "position": "above_below",
            },
        },
    ]

    relationships = [
        {"id": "r1", "kind": "partOf", "sourceGUID": plate_guid,
         "targetGUID": comp_guid, "confidence": "observed"},
        {"id": "r2", "kind": "partOf", "sourceGUID": bolt_guid,
         "targetGUID": comp_guid, "confidence": "observed"},
        {"id": "r3", "kind": "partOf", "sourceGUID": weld_guid,
         "targetGUID": comp_guid, "confidence": "observed"},
        {"id": "r4", "kind": "boltedTo", "sourceGUID": bolt_guid,
         "targetGUID": plate_guid, "confidence": "observed"},
        {"id": "r5", "kind": "boltedTo", "sourceGUID": bolt_guid,
         "targetGUID": "5213", "confidence": "inferred"},
        {"id": "r6", "kind": "weldedTo", "sourceGUID": weld_guid,
         "targetGUID": plate_guid, "confidence": "observed"},
        {"id": "r7", "kind": "weldedTo", "sourceGUID": weld_guid,
         "targetGUID": "5165", "confidence": "inferred"},
        {"id": "r8", "kind": "memberOf", "sourceGUID": "5165",
         "targetGUID": comp_guid, "confidence": "observed"},
        {"id": "r9", "kind": "secondaryOf", "sourceGUID": "5213",
         "targetGUID": comp_guid, "confidence": "observed"},
    ]

    operations = [
        {
            "id": "op1", "type": "BoltHole", "targetPartGUID": plate_guid,
            "operatorGUID": bolt_guid,
            "confidence": "observed",
        },
    ]

    parameters = [
        {"name": "plateThickness", "type": "Length", "value": 10.0, "units": "mm",
         "source": "FIXTURE-V3", "readable": "ok", "confidence": "observed"},
        {"name": "plateWidth", "type": "Length", "value": 170.0, "units": "mm",
         "source": "FIXTURE-V3", "readable": "ok", "confidence": "observed"},
        {"name": "plateHeight", "type": "Length", "value": 200.0, "units": "mm",
         "source": "FIXTURE-V3", "readable": "ok", "confidence": "observed"},
        {"name": "boltCount", "type": "Integer", "value": 6, "units": "",
         "source": "FIXTURE-V3", "readable": "ok", "confidence": "observed"},
        {"name": "boltSize", "type": "Enum", "value": "M20", "units": "",
         "source": "FIXTURE-V3", "readable": "ok", "confidence": "observed"},
    ]

    diagnostics = {
        "knownLimitations": [
            "FIXTURE-V3: elle yazilmis test verisi.",
            "plaka kati delik topolojisi icermiyor; delikler bulon verisinden yeniden uretilir.",
        ],
        "inaccessibleObjects": [],
        "unsupportedFeatures": [
            "kaynak geometrisi yeniden uretilmez (yalniz metadata).",
            "ikincil uye (HEA300 BEAM) govdesi yeniden uretilmez (yalniz referans).",
        ],
        "warnings": [],
    }

    write_json(root, "manifest.json", manifest)
    write_json(root, "component.json", comp)
    write_json(root, "parameters.json", parameters)
    write_json(root, "objects.json", objects)
    write_json(root, "relationships.json", relationships)
    write_json(root, "operations.json", operations)
    write_json(root, "diagnostics.json", diagnostics)
    write_json(root, "geometry/0000.json", {
        "formatVersion": "3.0", "type": "bbox", "representation": "reconstructed",
        "bbox": plate_bbox,
    })

    finalize(root, ["manifest.json", "component.json", "parameters.json",
                    "objects.json", "relationships.json", "operations.json",
                    "diagnostics.json", "geometry/0000.json"])


def make_144(root):
    plate_guid = "7701"
    stiff1_guid = "7702"
    stiff2_guid = "7703"
    bolt1_guid = "7704"
    bolt2_guid = "7705"
    weld1_guid = "7706"
    weld2_guid = "7707"
    fitting_guid = "7708"
    boolean_guid = "7709"
    comp_guid = "1441"

    manifest = base_manifest("3.0", 144, "End plate (144)", "144-1441")
    manifest["exportedObjectCount"] = 9

    comp = component(144, "End plate (144)", comp_guid,
                     member("6100", "HEA400", "S235JR", "COLUMN",
                            vec(0, 0, 0), vec(0, 0, 4000)),
                     [member("6200", "IPE300", "S235JR", "BEAM",
                             vec(0, 0, 2000), vec(4000, 0, 2000))])

    end_plate_bbox = {"min": vec(-110, -300, 1850), "max": vec(10, 300, 2250)}

    objects = [
        {
            "sourceGUID": plate_guid, "identifierId": 7701,
            "kind": "ContourPlate", "name": "END PLATE",
            "roleKey": "end_plate", "profile": "PL15", "material": "S235JR",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "Plate",
            "geometryFile": "geometry/0000.json",
            "geometryRepresentation": "approximate", "confidence": "observed",
            "topology": {"faceCount": 8, "loopCount": 8,
                         "faceVertexOccurrences": 40, "holeLoops": 8},
        },
        {
            "sourceGUID": stiff1_guid, "identifierId": 7702,
            "kind": "ContourPlate", "name": "STIFFENER",
            "roleKey": "top_stiffener", "profile": "PL10", "material": "S235JR",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "Plate",
            "geometryFile": "geometry/0001.json",
            "geometryRepresentation": "reconstructed", "confidence": "observed",
        },
        {
            "sourceGUID": stiff2_guid, "identifierId": 7703,
            "kind": "ContourPlate", "name": "STIFFENER",
            "roleKey": "bottom_stiffener", "profile": "PL10", "material": "S235JR",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "Plate",
            "geometryFile": "geometry/0002.json",
            "geometryRepresentation": "reconstructed", "confidence": "observed",
        },
        {
            "sourceGUID": bolt1_guid, "identifierId": 7704,
            "kind": "BoltGroup", "name": "BOLT",
            "roleKey": "flange_bolts",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "BoltGroup",
            "boltGroup": {
                "size": "M20", "standardCode": 7990, "tolerance": 2.0,
                "partToBeBolted": "6200", "partToBoltTo": plate_guid,
                "axis": "z", "axisSource": "inferred",
                "bolts": [
                    {"position": vec(-50, -200, 2050), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                    {"position": vec(-50, 200, 2050), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                    {"position": vec(-50, -200, 2150), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                    {"position": vec(-50, 200, 2150), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                ],
            },
        },
        {
            "sourceGUID": bolt2_guid, "identifierId": 7705,
            "kind": "BoltGroup", "name": "BOLT",
            "roleKey": "web_bolts",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "BoltGroup",
            "boltGroup": {
                "size": "M20", "standardCode": 7990, "tolerance": 2.0,
                "partToBeBolted": "6200", "partToBoltTo": plate_guid,
                "axis": "z", "axisSource": "inferred",
                "bolts": [
                    {"position": vec(-50, -40, 2000), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                    {"position": vec(-50, 40, 2000), "nominalDiameter": 20,
                     "holeDiameterSource": "standard"},
                ],
            },
        },
        {
            "sourceGUID": weld1_guid, "identifierId": 7706,
            "kind": "Weld", "name": "WELD",
            "roleKey": "flange_weld",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "Weld",
            "weld": {
                "type": "fillet", "size": 6.0, "sizeAbove": 6.0, "sizeBelow": 6.0,
                "mainSourceGUID": "6100", "secondarySourceGUID": plate_guid,
                "position": "above_below",
            },
        },
        {
            "sourceGUID": weld2_guid, "identifierId": 7707,
            "kind": "Weld", "name": "WELD",
            "roleKey": "web_weld",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "Weld",
            "weld": {
                "type": "fillet", "size": 5.0, "sizeAbove": 5.0, "sizeBelow": 5.0,
                "mainSourceGUID": "6200", "secondarySourceGUID": plate_guid,
                "position": "above_below",
            },
        },
        {
            "sourceGUID": fitting_guid, "identifierId": 7708,
            "kind": "Fitting", "name": "FITTING",
            "roleKey": "beam_fitting",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "Fitting",
            "confidence": "inferred",
            "fitting": {
                "plane": {"origin": vec(0, 0, 2000), "normal": vec(1, 0, 0)},
                "normal": vec(1, 0, 0),
                "targetPartGUID": "6200",
                "keepSide": "negative",
                "inferred": True,
            },
        },
        {
            "sourceGUID": boolean_guid, "identifierId": 7709,
            "kind": "BooleanPart", "name": "CUT",
            "roleKey": "beam_cut",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "Cut",
            "confidence": "inferred",
            "booleanPart": {
                "operationType": "Cut",
                "targetPartGUID": "6200",
                "operandGUID": "6100",
                "inferred": True,
            },
        },
    ]

    relationships = [
        {"id": "r1", "kind": "partOf", "sourceGUID": plate_guid,
         "targetGUID": comp_guid, "confidence": "observed"},
        {"id": "r2", "kind": "partOf", "sourceGUID": stiff1_guid,
         "targetGUID": comp_guid, "confidence": "observed"},
        {"id": "r3", "kind": "partOf", "sourceGUID": stiff2_guid,
         "targetGUID": comp_guid, "confidence": "observed"},
        {"id": "r4", "kind": "boltedTo", "sourceGUID": bolt1_guid,
         "targetGUID": plate_guid, "confidence": "observed"},
        {"id": "r5", "kind": "boltedTo", "sourceGUID": bolt2_guid,
         "targetGUID": plate_guid, "confidence": "observed"},
        {"id": "r6", "kind": "weldedTo", "sourceGUID": weld1_guid,
         "targetGUID": plate_guid, "confidence": "observed"},
        {"id": "r7", "kind": "weldedTo", "sourceGUID": weld2_guid,
         "targetGUID": plate_guid, "confidence": "observed"},
        {"id": "r8", "kind": "memberOf", "sourceGUID": "6100",
         "targetGUID": comp_guid, "confidence": "observed"},
        {"id": "r9", "kind": "secondaryOf", "sourceGUID": "6200",
         "targetGUID": comp_guid, "confidence": "observed"},
        {"id": "r10", "kind": "cutBy", "sourceGUID": "6200",
         "targetGUID": comp_guid, "confidence": "inferred"},
        {"id": "r11", "kind": "fittedTo", "sourceGUID": fitting_guid,
         "targetGUID": "6200", "confidence": "inferred"},
        {"id": "r12", "kind": "operatedBy", "sourceGUID": boolean_guid,
         "targetGUID": "6200", "confidence": "inferred"},
    ]

    operations = [
        {"id": "op1", "type": "BoltHole", "targetPartGUID": plate_guid,
         "operatorGUID": bolt1_guid, "confidence": "observed"},
        {"id": "op2", "type": "BoltHole", "targetPartGUID": plate_guid,
         "operatorGUID": bolt2_guid, "confidence": "observed"},
        {"id": "op3", "type": "Fitting", "targetPartGUID": "6200",
         "operatorGUID": "1441",
         "fittingPlane": {"origin": vec(0, 0, 2000), "normal": vec(1, 0, 0)},
         "keepSide": "negative", "inferred": True, "confidence": "inferred"},
    ]

    parameters = [
        {"name": "endPlateThickness", "type": "Length", "value": 15.0, "units": "mm",
         "source": "FIXTURE-V3", "readable": "ok", "confidence": "observed"},
        {"name": "boltRows", "type": "Integer", "value": 2, "units": "",
         "source": "FIXTURE-V3", "readable": "ok", "confidence": "observed"},
        {"name": "boltSize", "type": "Enum", "value": "M20", "units": "",
         "source": "FIXTURE-V3", "readable": "ok", "confidence": "observed"},
    ]

    diagnostics = {
        "knownLimitations": ["FIXTURE-V3: elle yazilmis cok parcali test verisi."],
        "inaccessibleObjects": [],
        "unsupportedFeatures": [
            "kaynak geometrisi yeniden uretilmez (yalniz metadata).",
            "kolon/kiris govdesi yeniden uretilmez (yalniz referans).",
            "fitting hedefi uye referansi (6200); govde yeniden uretilmediginden fitting gosterim disi.",
        ],
        "warnings": [],
    }

    write_json(root, "manifest.json", manifest)
    write_json(root, "component.json", comp)
    write_json(root, "parameters.json", parameters)
    write_json(root, "objects.json", objects)
    write_json(root, "relationships.json", relationships)
    write_json(root, "operations.json", operations)
    write_json(root, "diagnostics.json", diagnostics)
    # end plate: mesh (approximate, icinde delikler var)
    write_json(root, "geometry/0000.json", {
        "formatVersion": "3.0", "type": "mesh", "representation": "approximate",
        "bbox": end_plate_bbox,
        "mesh": box_mesh((end_plate_bbox["min"]["x"], end_plate_bbox["min"]["y"],
                          end_plate_bbox["min"]["z"]),
                         (end_plate_bbox["max"]["x"], end_plate_bbox["max"]["y"],
                          end_plate_bbox["max"]["z"])),
    })
    # stiffeners: bbox (reconstructed)
    write_json(root, "geometry/0001.json", {
        "formatVersion": "3.0", "type": "bbox", "representation": "reconstructed",
        "bbox": {"min": vec(-110, -300, 1850), "max": vec(-100, 300, 2050)},
    })
    write_json(root, "geometry/0002.json", {
        "formatVersion": "3.0", "type": "bbox", "representation": "reconstructed",
        "bbox": {"min": vec(-110, -300, 2050), "max": vec(-100, 300, 2250)},
    })

    finalize(root, ["manifest.json", "component.json", "parameters.json",
                    "objects.json", "relationships.json", "operations.json",
                    "diagnostics.json", "geometry/0000.json",
                    "geometry/0001.json", "geometry/0002.json"])


def make_mesh_box(root):
    comp_guid = "9001"
    plate_guid = "9002"

    manifest = base_manifest("3.0", 0, "Mesh box probe", "mesh-9001")
    manifest["sourceComponentType"] = "Detail"
    manifest["exportedObjectCount"] = 1

    comp = component(0, "Mesh box probe", comp_guid,
                     member("9100", "HEA400", "S235JR", "COLUMN"), [])

    bbox = {"min": vec(4.25, 195, 6959), "max": vec(14.25, 365, 7159)}
    objects = [
        {
            "sourceGUID": plate_guid, "identifierId": 9002,
            "kind": "ContourPlate", "name": "PLATE",
            "roleKey": "main_plate", "profile": "PL10", "material": "S235JR",
            "parentSourceGUID": comp_guid,
            "mapped": True, "mappedKind": "Plate",
            "geometryFile": "geometry/0000.json",
            "geometryRepresentation": "approximate", "confidence": "observed",
        },
    ]
    relationships = [
        {"id": "r1", "kind": "partOf", "sourceGUID": plate_guid,
         "targetGUID": comp_guid, "confidence": "observed"},
    ]
    operations = []
    parameters = []
    diagnostics = {
        "knownLimitations": ["FIXTURE-V3: tek mesh kutu (mesh->solid yolu testi)."],
        "inaccessibleObjects": [],
        "unsupportedFeatures": [],
        "warnings": [],
    }

    write_json(root, "manifest.json", manifest)
    write_json(root, "component.json", comp)
    write_json(root, "parameters.json", parameters)
    write_json(root, "objects.json", objects)
    write_json(root, "relationships.json", relationships)
    write_json(root, "operations.json", operations)
    write_json(root, "diagnostics.json", diagnostics)
    write_json(root, "geometry/0000.json", {
        "formatVersion": "3.0", "type": "mesh", "representation": "approximate",
        "bbox": bbox,
        "mesh": box_mesh((4.25, 195, 6959), (14.25, 365, 7159)),
    })

    finalize(root, ["manifest.json", "component.json", "parameters.json",
                    "objects.json", "relationships.json", "operations.json",
                    "diagnostics.json", "geometry/0000.json"])


def main():
    out_root = sys.argv[1] if len(sys.argv) > 1 else FIXTURES_DIR
    make_146(os.path.join(out_root, "fixture-146-shear-plate-v3.mmbridge"))
    make_144(os.path.join(out_root, "fixture-144-end-plate-v3.mmbridge"))
    make_mesh_box(os.path.join(out_root, "fixture-mesh-box-v3.mmbridge"))
    print("UREtILDI: %s" % out_root)


if __name__ == "__main__":
    main()
