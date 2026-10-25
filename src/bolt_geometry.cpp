#ifdef MM_HAS_OCC
#include "model_maker/bolt_geometry.hpp"

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>

namespace mm {

// Eksen vektorunu birim gp_Dir'e cevirir; dejenere ise false.
static bool boltAxisDir(const Vec3& axis, gp_Dir& out) noexcept {
    const double len =
        std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len < 1e-12) return false;
    out = gp_Dir(axis.x / len, axis.y / len, axis.z / len);
    return true;
}

// Eksene dik ortonormal (u, v) tabani. z ∥ ref durumunda farkli ref denenir.
static bool boltFrame(const gp_Dir& z, gp_Dir& u, gp_Dir& v) noexcept {
    gp_Dir ref(0.0, 0.0, 1.0);
    if (std::abs(z.Z()) > 0.9) ref = gp_Dir(1.0, 0.0, 0.0);
    u = ref.Crossed(z);  // z'ye dik
    if (u.X() * u.X() + u.Y() * u.Y() + u.Z() * u.Z() < 1e-24) return false;
    v = z.Crossed(u);    // z ve u'ya dik
    return true;
}

// gp_Pnt'i eksen (birim gp_Dir) boyunca `distance` kadar oteleyen yardimci.
static gp_Pnt shift(const gp_Pnt& c, const gp_Dir& z, double distance) noexcept {
    return c.Translated(gp_Vec(z) * distance);
}

TopoDS_Shape boltHexPrism(const Vec3& center, const Vec3& axis,
                          double acrossFlats, double height) {
    if (acrossFlats <= 0.0 || height <= 0.0) return TopoDS_Shape();
    gp_Dir z;
    if (!boltAxisDir(axis, z)) return TopoDS_Shape();
    gp_Dir u, v;
    if (!boltFrame(z, u, v)) return TopoDS_Shape();
    const double R = acrossFlats / std::sqrt(3.0);  // regular hexagon circumradius
    const gp_Pnt c(center.x, center.y, center.z);
    const gp_Pnt base = shift(c, z, -height * 0.5);  // eksen boyunca ortali
    BRepBuilderAPI_MakePolygon poly;
    for (int i = 0; i < 6; ++i) {
        const double ang = static_cast<double>(i) * M_PI / 3.0;
        const gp_Vec off = gp_Vec(u) * (R * std::cos(ang)) +
                           gp_Vec(v) * (R * std::sin(ang));
        poly.Add(base.Translated(off));
    }
    poly.Close();
    BRepBuilderAPI_MakeFace face(poly.Wire());
    if (!face.IsDone()) return TopoDS_Shape();
    return BRepPrimAPI_MakePrism(face.Face(), gp_Vec(z) * height).Shape();
}

TopoDS_Shape boltWasherDisc(const Vec3& center, const Vec3& axis,
                            double outerDiameter, double thickness) {
    if (outerDiameter <= 0.0 || thickness <= 0.0) return TopoDS_Shape();
    gp_Dir z;
    if (!boltAxisDir(axis, z)) return TopoDS_Shape();
    const gp_Pnt c(center.x, center.y, center.z);
    const gp_Pnt base = shift(c, z, -thickness * 0.5);
    return BRepPrimAPI_MakeCylinder(gp_Ax2(base, z), outerDiameter * 0.5, thickness)
        .Shape();
}

TopoDS_Shape boltShank(const Vec3& origin, const Vec3& axis,
                       double nominalDiameter, double gripLength) {
    if (nominalDiameter <= 0.0 || gripLength <= 0.0) return TopoDS_Shape();
    gp_Dir z;
    if (!boltAxisDir(axis, z)) return TopoDS_Shape();
    const gp_Pnt c(origin.x, origin.y, origin.z);
    const gp_Pnt base = shift(c, z, -gripLength * 0.5);
    return BRepPrimAPI_MakeCylinder(gp_Ax2(base, z), nominalDiameter * 0.5,
                                    gripLength)
        .Shape();
}

TopoDS_Shape buildBoltAssemblyShape(const BoltGroup& bolt, double gripLength) {
    if (bolt.params.nominalDiameter <= 0.0) return TopoDS_Shape();
    gp_Dir z;
    if (!boltAxisDir(bolt.axis, z)) return TopoDS_Shape();
    const double grip = gripLength > 0.0 ? gripLength : 0.0;
    const BoltDimensions d = boltDimensions(bolt.params.nominalDiameter);
    const gp_Pnt c(bolt.origin.x, bolt.origin.y, bolt.origin.z);

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

    const double halfGrip = grip * 0.5;
    const double washerT = bolt.params.hasWasher ? d.washerThickness : 0.0;
    // BOLT DIRECTION (Tekla bolting direction): 1 = bas -eksen (kolona), 2 = bas
    // +eksen (kirise). dir ile bas/somun taraflari yer degistirir.
    const double dir = (bolt.params.direction == 2) ? -1.0 : 1.0;

    // Bas tarafi (direction 1: -eksen yonu): bas + bas pulu.
    if (bolt.params.hasHead) {
        const gp_Pnt headCenter =
            shift(c, z, -dir * (halfGrip + washerT + d.headHeight * 0.5));
        fuse(boltHexPrism({headCenter.X(), headCenter.Y(), headCenter.Z()},
                          bolt.axis, d.headAcrossFlats, d.headHeight));
    }
    if (bolt.params.hasWasher) {
        const gp_Pnt headWasher =
            shift(c, z, -dir * (halfGrip + d.washerThickness * 0.5));
        fuse(boltWasherDisc({headWasher.X(), headWasher.Y(), headWasher.Z()},
                            bolt.axis, d.washerOuterDiameter, d.washerThickness));
    }
    // Govde (grip boyunca). lengthIncrease (Tekla "Bolt length increase", boya payi
    // vb.) govdeyi SOMUN tarafina uzatir: govde merkezi dir*inc/2 kayar, somun ve
    // somun pulu dir*inc kayar; bas tarafi degismez.
    const double inc = std::max(0.0, bolt.params.lengthIncrease);
    {
        const gp_Pnt shankCenter = shift(c, z, dir * inc * 0.5);
        fuse(boltShank({shankCenter.X(), shankCenter.Y(), shankCenter.Z()}, bolt.axis,
                       bolt.params.nominalDiameter, grip + inc));
    }
    // Somun tarafi (direction 1: +eksen yonu): somun pulu + somun.
    if (bolt.params.hasWasher) {
        const gp_Pnt nutWasher =
            shift(c, z, dir * (halfGrip + inc + d.washerThickness * 0.5));
        fuse(boltWasherDisc({nutWasher.X(), nutWasher.Y(), nutWasher.Z()},
                            bolt.axis, d.washerOuterDiameter, d.washerThickness));
    }
    if (bolt.params.hasNut) {
        const gp_Pnt nutCenter =
            shift(c, z, dir * (halfGrip + inc + washerT + d.nutHeight * 0.5));
        fuse(boltHexPrism({nutCenter.X(), nutCenter.Y(), nutCenter.Z()},
                          bolt.axis, d.nutAcrossFlats, d.nutHeight));
    }

    return result;
}

TopoDS_Shape buildBoltGroupAssemblyShape(const BoltGroup& bolt, double gripLength) {
    const std::vector<Vec3> origins = boltGroupBoltOrigins(bolt);
    TopoDS_Shape result;
    bool have = false;
    for (const Vec3& o : origins) {
        BoltGroup one = bolt;
        one.origin = o;
        TopoDS_Shape s = buildBoltAssemblyShape(one, gripLength);
        if (s.IsNull()) continue;
        if (!have) {
            result = s;
            have = true;
        } else {
            BRepAlgoAPI_Fuse f(result, s);
            if (f.IsDone()) result = f.Shape();
        }
    }
    return result;
}

} // namespace mm
#endif // MM_HAS_OCC
