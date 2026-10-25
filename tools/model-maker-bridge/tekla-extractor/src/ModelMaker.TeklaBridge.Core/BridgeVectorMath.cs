using System;

namespace ModelMaker.TeklaBridge.Core
{
    // Tekla CoordinateSystem AxisX/AxisY can be scaled directions (e.g. 1000 mm).
    // Bridge stores ORTHONORMAL, dimensionless basis directions.
    // Invalid/degenerate source coordinates must fail, not silently create axes.
    public static class BridgeVectorMath
    {
        public static Vec3 Unit(Vec3 v)
        {
            if (v == null) throw new ArgumentNullException(nameof(v));
            double length = Math.Sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
            if (double.IsNaN(length) || double.IsInfinity(length) || length < 1e-12)
                throw new InvalidOperationException("Cannot normalize an invalid or zero direction.");
            return new Vec3 { X = v.X / length, Y = v.Y / length, Z = v.Z / length };
        }

        public static Vec3 UnitCross(Vec3 a, Vec3 b)
        {
            if (a == null || b == null) throw new ArgumentNullException();
            return Unit(new Vec3
            {
                X = a.Y * b.Z - a.Z * b.Y,
                Y = a.Z * b.X - a.X * b.Z,
                Z = a.X * b.Y - a.Y * b.X
            });
        }

        public static CoordinateSystem OrthonormalFrame(Vec3 origin, Vec3 rawX, Vec3 rawY)
        {
            if (origin == null) throw new ArgumentNullException(nameof(origin));
            var x = Unit(rawX);
            // Remove any numerical component of rawY parallel to x (Gram-Schmidt).
            if (rawY == null) throw new ArgumentNullException(nameof(rawY));
            double dot = x.X * rawY.X + x.Y * rawY.Y + x.Z * rawY.Z;
            var y = Unit(new Vec3
            {
                X = rawY.X - dot * x.X,
                Y = rawY.Y - dot * x.Y,
                Z = rawY.Z - dot * x.Z
            });
            var z = UnitCross(x, y);
            return new CoordinateSystem { Origin = origin, XAxis = x, YAxis = y, ZAxis = z };
        }
    }
}
