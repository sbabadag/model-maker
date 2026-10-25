using System;
using Xunit;

namespace ModelMaker.TeklaBridge.Core.Tests
{
    public class BridgeVectorMathTests
    {
        [Fact]
        public void Real146AxesAreUnitLengthAndRightHanded()
        {
            var f = BridgeVectorMath.OrthonormalFrame(
                new Vec3 { X = 10, Y = 20, Z = 30 },
                new Vec3 { X = 0, Y = 1000, Z = 0 },
                new Vec3 { X = 0, Y = 0, Z = 1000 });
            Assert.Equal(0.0, f.XAxis.X, 10);
            Assert.Equal(1.0, f.XAxis.Y, 10);
            Assert.Equal(1.0, f.YAxis.Z, 10);
            Assert.Equal(1.0, f.ZAxis.X, 10);
            Assert.Equal(0.0, f.ZAxis.Y, 10);
            Assert.Equal(30.0, f.Origin.Z, 10);
        }

        [Fact]
        public void FittingPlaneNormalMagnitudeIsOne()
        {
            var n = BridgeVectorMath.UnitCross(
                new Vec3 { X = 0, Y = 0, Z = 700 },
                new Vec3 { X = 700, Y = 0, Z = 0 });
            Assert.Equal(0.0, n.X, 10);
            Assert.Equal(1.0, n.Y, 10);
            Assert.Equal(0.0, n.Z, 10);
        }

        [Fact]
        public void SlightlySkewedAxesAreOrthogonalized()
        {
            var f = BridgeVectorMath.OrthonormalFrame(
                new Vec3(),
                new Vec3 { X = 1000, Y = 0, Z = 0 },
                new Vec3 { X = 300, Y = 1000, Z = 0 });
            Assert.Equal(1.0, f.XAxis.X, 10);
            Assert.Equal(0.0, f.YAxis.X, 10);
            Assert.Equal(1.0, f.YAxis.Y, 10);
            Assert.Equal(1.0, f.ZAxis.Z, 10);
        }

        [Fact]
        public void BoltFrameProbeSerializesWithoutAuthorizingDrilling()
        {
            var bg = new BoltGroupInfo
            {
                Size = "M20",
                Axis = "",
                AxisSource = "",
                Direction = null,
                FrameProbe = new BoltFrameProbe
                {
                    CoordinateSystem = BridgeVectorMath.OrthonormalFrame(
                        new Vec3 { X = 0, Y = 0, Z = 0 },
                        new Vec3 { X = 0, Y = 1000, Z = 0 },
                        new Vec3 { X = 0, Y = 0, Z = 1000 }),
                    NormalCandidate = new Vec3 { X = 1, Y = 0, Z = 0 },
                    VerifiedForDrilling = false
                },
                HoleSettingsProbe = new BoltHoleSettingsProbe
                {
                    PlainHoleType = "HOLE_TYPE_THROUGH",
                    Hole1 = true,
                    VerifiedForDrilling = false
                }
            };
            var json = Newtonsoft.Json.JsonConvert.SerializeObject(bg);
            Assert.Contains("\"frameProbe\"", json);
            Assert.Contains("\"holeSettingsProbe\"", json);
            var copy = Newtonsoft.Json.JsonConvert.DeserializeObject<BoltGroupInfo>(json);
            Assert.NotNull(copy.FrameProbe);
            Assert.Equal(1.0, copy.FrameProbe.CoordinateSystem.ZAxis.X, 10);
            Assert.False(copy.FrameProbe.VerifiedForDrilling);
            Assert.False(copy.HoleSettingsProbe.VerifiedForDrilling);
            Assert.True(copy.HoleSettingsProbe.Hole1);
            Assert.Equal("", copy.Axis);
            Assert.Null(copy.Direction);
        }

        [Fact]
        public void PerPlyHoleMetadataIsReadableButNotDrillAuthority()
        {
            var probe = new BoltHoleSettingsProbe
            {
                BoltPresent = true,
                PlainHoleType = "HOLE_TYPE_THROUGH",
                SpecialHoleType = "HOLE_TYPE_SLOTTED",
                Hole1 = false,
                Hole2 = false,
                Hole3 = false,
                Hole4 = false,
                Hole5 = false,
                VerifiedForDrilling = false,
                PerPlySpecialHoles = new System.Collections.Generic.List<BoltPlyHoleProbe>
                {
                    new BoltPlyHoleProbe
                    {
                        PlyIndex = 1, SpecialHoleType = "HOLE_TYPE_SLOTTED",
                        LongHoleX = 0.0, LongHoleY = 0.0
                    }
                }
            };
            var json = Newtonsoft.Json.JsonConvert.SerializeObject(probe);
            var restored = Newtonsoft.Json.JsonConvert.DeserializeObject<BoltHoleSettingsProbe>(json);
            Assert.True(restored.BoltPresent.HasValue && restored.BoltPresent.Value);
            Assert.False(restored.VerifiedForDrilling);
            Assert.False(restored.Hole1);
            Assert.Single(restored.PerPlySpecialHoles);
            Assert.Equal(1, restored.PerPlySpecialHoles[0].PlyIndex);
        }

        [Fact]
        public void PartHasHolesReportIsNotPermissionForSixPhysicalCuts()
        {
            var rec = new ObjectRecord
            {
                SourceGUID = "plate-146",
                Kind = "ContourPlate",
                PlateHoleProbe = new PlateHoleReportProbe
                {
                    ReportReadable = true,
                    ReportValue = 1,
                    HasBoltHoles = true,
                    RawTopology = new SolidTopologyProbe
                    {
                        FaceCount = 6, LoopCount = 6, HoleLoops = 0
                    },
                    NormalTopology = new SolidTopologyProbe
                    {
                        FaceCount = 6, LoopCount = 6, HoleLoops = 0
                    },
                    TopologyDiffers = false,
                    VerifiedHoleCount = null,
                    VerifiedForDrilling = false
                }
            };
            var json = Newtonsoft.Json.JsonConvert.SerializeObject(rec);
            var parsed = Newtonsoft.Json.JsonConvert.DeserializeObject<ObjectRecord>(json);
            Assert.NotNull(parsed.PlateHoleProbe);
            Assert.Equal(1, parsed.PlateHoleProbe.ReportValue);
            Assert.True(parsed.PlateHoleProbe.HasBoltHoles.HasValue &&
                        parsed.PlateHoleProbe.HasBoltHoles.Value);
            Assert.False(parsed.PlateHoleProbe.TopologyDiffers.Value);
            Assert.Null(parsed.PlateHoleProbe.VerifiedHoleCount);
            Assert.False(parsed.PlateHoleProbe.VerifiedForDrilling);
        }

        [Fact]
        public void HoleReportUnavailabilityAndRegularHoleMinusOneRoundtrip()
        {
            var probe = new BoltHoleSettingsProbe
            {
                VerifiedForDrilling = false,
                ReportProbes = new System.Collections.Generic.List<BoltReportValueProbe>
                {
                    new BoltReportValueProbe { Name = "HOLE_1_TYPE", Readable = true, Value = "-1" },
                    new BoltReportValueProbe { Name = "DIAMETER_X", Readable = false, Value = null }
                }
            };
            var json = Newtonsoft.Json.JsonConvert.SerializeObject(probe);
            var restored = Newtonsoft.Json.JsonConvert.DeserializeObject<BoltHoleSettingsProbe>(json);
            Assert.Equal(2, restored.ReportProbes.Count);
            Assert.Equal("-1", restored.ReportProbes[0].Value);
            Assert.True(restored.ReportProbes[0].Readable);
            Assert.False(restored.ReportProbes[1].Readable);
            Assert.Null(restored.ReportProbes[1].Value);
            Assert.False(restored.VerifiedForDrilling);
        }

        [Fact]
        public void DegenerateAxesAreRejectedInsteadOfInvented()
        {
            Assert.Throws<InvalidOperationException>(() =>
                BridgeVectorMath.OrthonormalFrame(
                    new Vec3(),
                    new Vec3 { X = 1000 },
                    new Vec3 { X = 2000 }));
            Assert.Throws<InvalidOperationException>(() =>
                BridgeVectorMath.UnitCross(
                    new Vec3 { X = 0, Y = 0, Z = 0 },
                    new Vec3 { X = 0, Y = 1, Z = 0 }));
        }
    }
}
