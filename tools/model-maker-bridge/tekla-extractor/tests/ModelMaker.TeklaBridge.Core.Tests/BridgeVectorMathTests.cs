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
