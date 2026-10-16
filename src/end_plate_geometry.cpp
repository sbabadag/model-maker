#ifdef MM_HAS_OCC
#include "model_maker/end_plate_geometry.hpp"
#include "model_maker/bolt_geometry.hpp"

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace mm {

static bool axisDir(const Vec3& a, gp_Dir& out) noexcept {
    const double len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    if (len < 1e-12) return false;
    out = gp_Dir(a.x / len, a.y / len, a.z / len);
    return true;
}

TopoDS_Shape buildEndPlateSolid(const Vec3& plateCenter, const Vec3& u, const Vec3& v,
                                const Vec3& axis, double plateWidth, double plateHeight,
                                double plateThickness) {
    if (plateWidth <= 0.0 || plateHeight <= 0.0 || plateThickness <= 0.0)
        return TopoDS_Shape();
    gp_Dir z;
    if (!axisDir(axis, z)) return TopoDS_Shape();
    gp_Dir uu, vv;
    if (!axisDir(u, uu)) return TopoDS_Shape();
    if (!axisDir(v, vv)) return TopoDS_Shape();

    const double hw = plateWidth * 0.5;
    const double hh = plateHeight * 0.5;
    const gp_Pnt c(plateCenter.x, plateCenter.y, plateCenter.z);
    // Dikdortgen duzlemini axis boyunca -thickness/2'ye kaydir (merkezden ortala).
    const gp_Pnt base = c.Translated(gp_Vec(z) * (-plateThickness * 0.5));

    BRepBuilderAPI_MakePolygon poly;
    const auto corner = [&](double su, double sv) {
        return base.Translated(gp_Vec(uu) * (su * hw) + gp_Vec(vv) * (sv * hh));
    };
    poly.Add(corner(-1.0, -1.0));
    poly.Add(corner(1.0, -1.0));
    poly.Add(corner(1.0, 1.0));
    poly.Add(corner(-1.0, 1.0));
    poly.Close();
    BRepBuilderAPI_MakeFace face(poly.Wire());
    if (!face.IsDone()) return TopoDS_Shape();
    return BRepPrimAPI_MakePrism(face.Face(), gp_Vec(z) * plateThickness).Shape();
}

TopoDS_Shape buildEndPlateFromPlacement(const EndPlatePlacement& placement) {
    return buildEndPlateSolid(placement.plateCenter, placement.u, placement.v, placement.axis,
                              placement.plateWidth, placement.plateHeight,
                              placement.plateThickness);
}

TopoDS_Shape buildEndPlateAssemblyShape(const EndPlatePlacement& placement, double gripLength) {
    TopoDS_Shape result;
    bool have = false;
    auto fuse = [&](const TopoDS_Shape& s) {
        if (s.IsNull()) return;
        if (!have) {
            result = s;
            have = true;
        } else {
            BRepAlgoAPI_Fuse f(result, s);
            if (f.IsDone()) result = f.Shape();
        }
    };
    fuse(buildEndPlateFromPlacement(placement));
    fuse(buildBoltGroupAssemblyShape(placement.bolts, gripLength));
    return result;
}

} // namespace mm
#endif // MM_HAS_OCC
