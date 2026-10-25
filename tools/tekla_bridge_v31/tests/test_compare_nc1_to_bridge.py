#!/usr/bin/env python3
"""Synthetic DSTV parity regressions: no proprietary Tekla files committed."""
import json
import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compare_nc1_to_bridge as compare


NC1 = """ST
** synthetic-example.nc1
1
1001
1
1001
S235JR
1
PL10
B
200.00
170.00
10.00
AK
v 0.00u 0.00 0 0 0 0 0
200.00 0.00 0 0 0 0 0
200.00 170.00 0 0 0 0 0
0.00 170.00 0 0 0 0 0
0.00 0.00 0 0 0 0 0
BO
v 40.00s 70.00 22.00
v 40.00s 130.00 22.00
v 100.00s 70.00 22.00
v 100.00s 130.00 22.00
v 160.00s 70.00 22.00
v 160.00s 130.00 22.00
EN
"""


class BridgeNcParityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "geometry").mkdir()
        self.nc = self.root / "1001.nc1"
        self.nc.write_text(NC1, encoding="ascii")
        self.objects = [
            {"kind": "ContourPlate", "sourceGUID": "plate-146",
             "profile": "PL10", "geometryFile": "geometry/0000.json",
             "plateHoleProbe": {
                 "partMark": "1001",
                 "dstvCoordinateSystem": {
                     "origin": {"x": 4.25, "y": 195, "z": 6959},
                     "xAxis": {"x": 0, "y": 0, "z": 1},
                     "yAxis": {"x": 0, "y": 1, "z": 0},
                     "zAxis": {"x": -1, "y": 0, "z": 0}
                 },
                 "hasBoltHoles": True, "verifiedForDrilling": False
             }},
            {"kind": "BoltGroup", "sourceGUID": "bolts-146",
             "boltGroup": {
                 "size": "M20", "tolerance": 2.0,
                 "partToBoltTo": "plate-146",
                 "axis": "", "axisSource": "",
                 "bolts": [
                     {"position": {"x": 0, "y": y, "z": z}}
                     for z in (6999, 7059, 7119) for y in (265, 325)
                 ]}}
        ]
        (self.root / "geometry/0000.json").write_text(json.dumps({
            "bbox": {"min": {"x": 4.25, "y": 195, "z": 6959},
                     "max": {"x": 14.25, "y": 365, "z": 7159}}
        }), encoding="utf-8")
        self.save_objects()

    def save_objects(self):
        (self.root / "objects.json").write_text(
            json.dumps(self.objects), encoding="utf-8")

    def test_nc1_six_22mm_holes_and_geometry_parity(self):
        report = compare.check(self.nc, self.root)
        self.assertEqual(len(report["nc"]["holes"]), 6)
        self.assertTrue(report["plateDimensionsMatch"])
        self.assertTrue(report["partTargetGuidMatches"])
        self.assertTrue(report["markMatchesNcHeader"])
        self.assertTrue(report["holeCountMatchesBoltPositions"])
        self.assertTrue(report["holeDiameterMatchesNominalPlusTolerance"])
        self.assertTrue(report["ncCentersMatchTeklaTransversePositions"])
        self.assertTrue(report["ncCentersInsideMeasuredPlateBBox"])
        self.assertFalse(report["drillingAuthorized"])

    def test_no_frame_remains_unverified(self):
        del self.objects[0]["plateHoleProbe"]["dstvCoordinateSystem"]
        self.save_objects()
        report = compare.check(self.nc, self.root)
        self.assertFalse(report["dstvFrameAvailable"])
        self.assertIsNone(report["ncCentersMatchTeklaTransversePositions"])
        self.assertFalse(report["drillingAuthorized"])

    def test_wrong_plate_identity_rejected(self):
        self.objects[1]["boltGroup"]["partToBoltTo"] = "wrong"
        self.save_objects()
        report = compare.check(self.nc, self.root)
        self.assertFalse(report["partTargetGuidMatches"])

    def test_non_matching_nc_diameter_rejected(self):
        self.nc.write_text(NC1.replace("22.00", "23.00"), encoding="ascii")
        report = compare.check(self.nc, self.root)
        self.assertFalse(report["holeDiameterMatchesNominalPlusTolerance"])


if __name__ == "__main__":
    unittest.main()
