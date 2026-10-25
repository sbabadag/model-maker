import hashlib
import json
import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bridge31_adapter import convert, vec

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "fixtures"


def data(d: Path, name: str):
    return json.loads((d / name).read_text(encoding="utf-8"))


class Bridge31Tests(unittest.TestCase):
    def convert_once(self, input_name: str):
        temp = tempfile.TemporaryDirectory()
        dest = Path(temp.name) / "pkg"
        result = convert(SOURCE / input_name, dest)
        self.addCleanup(temp.cleanup)
        return dest, result

    def check_compatible_v3_structure(self, dest: Path):
        """Mirrors required fields parsed by existing bridge_package.cpp; not an OCC integration test."""
        m = data(dest, "manifest.json")
        c = data(dest, "component.json")
        p = data(dest, "parameters.json")
        o = data(dest, "objects.json")
        d = data(dest, "diagnostics.json")
        self.assertEqual(m["formatVersion"], "3.0")
        self.assertTrue(m["componentIdentifier"])
        self.assertTrue(m["sourceComponentType"])
        self.assertEqual(c["guid"], m["componentIdentifier"])
        self.assertIsInstance(p, list)
        self.assertIsInstance(o, list)
        self.assertIsInstance(d, dict)
        self.assertEqual(m["exportedObjectCount"], len(o))
        ids = [x["sourceGUID"] for x in o]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertTrue(all(x.get("sourceGUID") and x.get("kind") for x in o))
        for x in o:
            if x.get("geometryFile"):
                rel = Path(x["geometryFile"])
                self.assertFalse(rel.is_absolute())
                self.assertNotIn("..", rel.parts)
                self.assertTrue((dest / rel).exists())
        for relation in data(dest, "relationships.json"):
            self.assertTrue(relation["kind"] and relation["sourceGUID"] and relation["targetGUID"])
        for op in data(dest, "operations.json"):
            self.assertTrue(op["id"] and op["type"] and op["targetPartGUID"])
        for name, expected in data(dest, "checksums.json").items():
            self.assertEqual(hashlib.sha256((dest / name).read_bytes()).hexdigest(), expected)

    def test_end_plate_144_synthetic(self):
        dest, r = self.convert_once("end_plate_144.json")
        self.check_compatible_v3_structure(dest)
        self.assertEqual(r[0]["number"], 144)
        self.assertEqual(len(data(dest, "parameters.json")), 10)
        self.assertEqual(len(data(dest, "objects.json")), 3)
        self.assertEqual(len(data(dest, "operations.json")), 1)
        self.assertEqual(data(dest, "operations.json")[0]["confidence"], "unresolved")
        bolts = next(x["boltGroup"] for x in data(dest, "objects.json") if x["kind"] == "BoltGroup")
        self.assertEqual(len(bolts["bolts"]), 8)
        self.assertEqual(bolts["axisSource"], "unresolved")
        self.assertFalse(any(b["holeDiameter"] for b in bolts["bolts"]))

    def test_real_146_probe(self):
        dest, r = self.convert_once("component-5245-v2.json")
        self.check_compatible_v3_structure(dest)
        self.assertEqual(r[0]["number"], 146)
        self.assertEqual(len(data(dest, "objects.json")), 8)
        self.assertEqual(len(data(dest, "parameters.json")), 0)
        self.assertEqual(data(dest, "component.json")["primaryMember"]["profile"], "HEA400")
        self.assertEqual(data(dest, "component.json")["secondaryMembers"][0]["profile"], "HEA300")
        objs = data(dest, "objects.json")
        self.assertEqual(next(x for x in objs if x["kind"] == "BoltGroup")["boltGroup"]["bolts"][0]["nominalDiameter"], 20)
        self.assertEqual(len(data(dest, "relationships.json")), 4)
        self.assertIn("Probe contains Tekla integer IDs", " ".join(data(dest, "diagnostics.json")["knownLimitations"]))
        self.assertEqual(next(x for x in objs if x["kind"] == "ContourPlate")["topology"]["faceCount"], 6)

    def test_older_haunch_probe_unverified_plane(self):
        dest, r = self.convert_once("component-990503.json")
        self.check_compatible_v3_structure(dest)
        self.assertEqual(r[0]["number"], 40)
        self.assertEqual(data(dest, "manifest.json")["coordinateSystem"], "unverified")
        self.assertEqual(r[0]["children"], 67)
        self.assertEqual(len(data(dest, "parameters.json")), 0)

    def test_invalid_schema_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as t:
            p = Path(t) / "source.json"
            p.write_text('{"schema":"bad"}')
            out = Path(t) / "output"
            with self.assertRaisesRegex(ValueError, "unsupported input schema"):
                convert(p, out)
            self.assertFalse(out.exists())

    def test_duplicate_guids_fails_without_partial_output(self):
        with tempfile.TemporaryDirectory() as t:
            src = json.loads((SOURCE / "end_plate_144.json").read_text())
            src["components"][0]["children"][1]["guid"] = src["components"][0]["children"][0]["guid"]
            p = Path(t) / "source.json"
            p.write_text(json.dumps(src))
            dest = Path(t) / "output"
            with self.assertRaisesRegex(ValueError, "duplicate source"):
                convert(p, dest)
            self.assertFalse(dest.exists())

    def test_multiple_v1_components_separated(self):
        with tempfile.TemporaryDirectory() as t:
            j = json.loads((SOURCE / "end_plate_144.json").read_text())
            j["components"].append(json.loads(json.dumps(j["components"][0])))
            j["components"][1]["guid"] = "second-component"
            p = Path(t) / "source.json"
            p.write_text(json.dumps(j))
            dest = Path(t) / "out"
            results = convert(p, dest)
            self.assertEqual(len(results), 2)
            self.assertTrue((dest / "component-000" / "manifest.json").exists())
            self.assertTrue((dest / "component-001" / "manifest.json").exists())

    def test_coordinate_values_reject_nan_inf(self):
        self.assertIsNone(vec([1, 2, float("nan")]))
        self.assertIsNone(vec([1, 2, float("inf")]))
        self.assertIsNone(vec([1, 2]))

    def test_no_overwrite(self):
        with tempfile.TemporaryDirectory() as t:
            target = Path(t) / "pkg"
            convert(SOURCE / "end_plate_144.json", target)
            with self.assertRaises(FileExistsError):
                convert(SOURCE / "end_plate_144.json", target)


if __name__ == "__main__":
    unittest.main(verbosity=2)
