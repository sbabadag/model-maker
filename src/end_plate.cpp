#include "model_maker/end_plate.hpp"

#include <cmath>

namespace mm {

static Vec3 unitOr(const Vec3& a, const Vec3& fallback) noexcept {
    const double len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    if (len < 1e-12) return fallback;
    return Vec3{a.x / len, a.y / len, a.z / len};
}

static Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

EndPlateFrame endPlateFrame(const Vec3& beamAxis, const Vec3& referenceNormal) noexcept {
    EndPlateFrame f;
    f.axis = unitOr(beamAxis, Vec3{0.0, 0.0, 1.0});
    // referenceNormal axis'e paralelse yatay genislik icin farkli ref dene.
    Vec3 ref = unitOr(referenceNormal, Vec3{0.0, 0.0, 1.0});
    const double dot = std::abs(f.axis.x * ref.x + f.axis.y * ref.y + f.axis.z * ref.z);
    if (dot > 0.9) ref = Vec3{1.0, 0.0, 0.0};
    f.u = unitOr(cross(ref, f.axis), Vec3{1.0, 0.0, 0.0});
    f.v = cross(f.axis, f.u);
    f.v = unitOr(f.v, Vec3{0.0, 1.0, 0.0});
    // f.axis, f.u, f.v sag-el ortonormal olmali (u x v = axis). Yeniden duzelt.
    f.v = cross(f.axis, f.u);
    f.v = unitOr(f.v, Vec3{0.0, 1.0, 0.0});
    return f;
}

EndPlatePlacement computeEndPlatePlacement(const Vec3& beamEnd, const Vec3& beamAxis,
                                           double secWidth, double secHeight,
                                           const EndPlateParams& params) noexcept {
    EndPlatePlacement out;
    const EndPlateFrame f = endPlateFrame(beamAxis, Vec3{0.0, 0.0, 1.0});
    out.u = f.u;
    out.v = f.v;
    out.axis = f.axis;
    out.plateWidth = secWidth + 2.0 * params.plateExtra;
    out.plateHeight = secHeight + 2.0 * params.plateExtra;
    out.plateThickness = params.plateThickness;
    if (out.plateWidth < 1.0) out.plateWidth = 1.0;
    if (out.plateHeight < 1.0) out.plateHeight = 1.0;

    // Levha merkezi = kiris ucu + axis * thickness/2 (levha kiris ucuna oturur,
    // kolona dogru uzanir).
    const double halfT = params.plateThickness * 0.5;
    out.plateCenter = Vec3{beamEnd.x + f.axis.x * halfT, beamEnd.y + f.axis.y * halfT,
                           beamEnd.z + f.axis.z * halfT};

    // Bulon grubunu kur. origin = levhanin sol-alt kosesi (u-, v- yonunde), edge iceri.
    const double hw = out.plateWidth * 0.5;
    const double hh = out.plateHeight * 0.5;
    const double ox = out.plateCenter.x - f.u.x * (hw - params.boltEdge) -
                      f.v.x * (hh - params.boltEdge);
    const double oy = out.plateCenter.y - f.u.y * (hw - params.boltEdge) -
                      f.v.y * (hh - params.boltEdge);
    const double oz = out.plateCenter.z - f.u.z * (hw - params.boltEdge) -
                      f.v.z * (hh - params.boltEdge);

    BoltGroup b;
    b.origin = Vec3{ox, oy, oz};
    b.axis = f.axis;
    b.xDir = f.u;
    b.countX = params.boltCountX > 0 ? params.boltCountX : 1;
    b.countY = params.boltCountY > 0 ? params.boltCountY : 1;
    b.spacingX = (b.countX > 1) ? (out.plateWidth - 2.0 * params.boltEdge) : 0.0;
    b.spacingY = (b.countY > 1) ? (out.plateHeight - 2.0 * params.boltEdge) /
                                      static_cast<double>(b.countY - 1)
                                : 0.0;
    b.params.nominalDiameter = params.boltDiameter;
    b.params.holeDiameter = params.holeDiameter;
    b.params.grade = params.boltGrade;
    b.params.hasHead = params.hasHead;
    b.params.hasNut = params.hasNut;
    b.params.hasWasher = params.hasWasher;
    out.bolts = b;
    return out;
}

} // namespace mm
