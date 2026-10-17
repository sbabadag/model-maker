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

// Bulon grubunu levha boyutlari + cerceveden kurar (iki yerlesim fonksiyonu
// ortak kullanir — tek dogruluk kaynagi).
static BoltGroup makeEndPlateBolts(const EndPlatePlacement& out,
                                   const EndPlateParams& params) noexcept {
    const double hw = out.plateWidth * 0.5;
    const double hh = out.plateHeight * 0.5;
    const double ox = out.plateCenter.x - out.u.x * (hw - params.boltEdge) -
                      out.v.x * (hh - params.boltEdge);
    const double oy = out.plateCenter.y - out.u.y * (hw - params.boltEdge) -
                      out.v.y * (hh - params.boltEdge);
    const double oz = out.plateCenter.z - out.u.z * (hw - params.boltEdge) -
                      out.v.z * (hh - params.boltEdge);
    BoltGroup b;
    b.origin = Vec3{ox, oy, oz};
    b.axis = out.axis;
    b.xDir = out.u;
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
    return b;
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

    out.bolts = makeEndPlateBolts(out, params);
    return out;
}

EndPlatePlacement computeEndPlatePlacementOnFace(const ReferencePlane& face, double gap,
                                                 double secWidth, double secHeight,
                                                 const EndPlateParams& params) noexcept {
    EndPlatePlacement out;
    // Kalinlik yonu = yuzey normali (kolondan DISARI, kirise dogru). face.normal
    // birim oldugu varsayilir ama endPlateFrame yeniden normalize eder.
    const EndPlateFrame f = endPlateFrame(face.normal, Vec3{0.0, 0.0, 1.0});
    out.u = f.u;
    out.v = f.v;
    out.axis = f.axis;
    out.plateWidth = secWidth + 2.0 * params.plateExtra;
    out.plateHeight = secHeight + 2.0 * params.plateExtra;
    out.plateThickness = params.plateThickness;
    if (out.plateWidth < 1.0) out.plateWidth = 1.0;
    if (out.plateHeight < 1.0) out.plateHeight = 1.0;

    // Levhanin kolon tarafindaki yuzu = face.origin + gap * normal; merkez,
    // bundan kalinlik/2 kadar normal yonunde (kirise dogru) oturur.
    const double halfT = params.plateThickness * 0.5;
    out.plateCenter = Vec3{face.origin.x + face.normal.x * (gap + halfT),
                           face.origin.y + face.normal.y * (gap + halfT),
                           face.origin.z + face.normal.z * (gap + halfT)};

    out.bolts = makeEndPlateBolts(out, params);
    return out;
}

} // namespace mm
