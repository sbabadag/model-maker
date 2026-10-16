#include "model_maker/bolt.hpp"

#include <cmath>

namespace mm {

BoltDimensions boltDimensions(double nominalDiameter) noexcept {
    BoltDimensions d;
    const double kd = nominalDiameter > 0.0 ? nominalDiameter : 1.0;
    d.headAcrossFlats = 1.5 * kd;
    d.headHeight = 0.7 * kd;
    d.nutAcrossFlats = 1.5 * kd;
    d.nutHeight = 0.8 * kd;
    d.washerOuterDiameter = 2.0 * kd;
    d.washerThickness = 0.15 * kd;
    return d;
}

CutModifier makeHoleCutModifier(EntityId part, const Vec3& origin,
                                const Vec3& axis, double holeDiameter) noexcept {
    CutModifier mod;
    mod.kind = CutKind::Hole;
    mod.target = part;
    mod.planeOrigin = origin;
    const double len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len > 0.0) {
        mod.planeNormal = Vec3{axis.x / len, axis.y / len, axis.z / len};
    } else {
        mod.planeNormal = Vec3{0.0, 0.0, 1.0};  // dejenere eksen: +Z varsayilan
    }
    mod.keepPositive = true;  // Hole icin anlamsiz (through-cut)
    mod.diameter = holeDiameter;
    mod.name = "Delik";
    return mod;
}

std::vector<Vec3> boltGroupBoltOrigins(const BoltGroup& bolt) noexcept {
    std::vector<Vec3> out;
    int nx = bolt.countX > 0 ? bolt.countX : 1;
    int ny = bolt.countY > 0 ? bolt.countY : 1;
    // Tekla/grup x yonu = xDir (normalize); grup y yonu = axis x xDir.
    const double axl = std::sqrt(bolt.axis.x * bolt.axis.x + bolt.axis.y * bolt.axis.y +
                                 bolt.axis.z * bolt.axis.z);
    Vec3 ax = axl > 1e-12 ? Vec3{bolt.axis.x / axl, bolt.axis.y / axl, bolt.axis.z / axl}
                          : Vec3{0.0, 0.0, 1.0};
    const double xl = std::sqrt(bolt.xDir.x * bolt.xDir.x + bolt.xDir.y * bolt.xDir.y +
                                bolt.xDir.z * bolt.xDir.z);
    Vec3 ux = xl > 1e-12 ? Vec3{bolt.xDir.x / xl, bolt.xDir.y / xl, bolt.xDir.z / xl}
                         : Vec3{1.0, 0.0, 0.0};
    // xDir eksene dik olmali; dik degilse (dosya/el giris) Gram-Schmidt ile duzelt.
    const double dot = ux.x * ax.x + ux.y * ax.y + ux.z * ax.z;
    Vec3 u = ux - Vec3{ax.x * dot, ax.y * dot, ax.z * dot};
    const double ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    if (ul < 1e-12) {
        // xDir eksene paralel; keyfi dik bir taban kur.
        Vec3 ref = (std::abs(ax.z) > 0.9) ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 0.0, 1.0};
        u = Vec3{ref.y * ax.z - ref.z * ax.y, ref.z * ax.x - ref.x * ax.z,
                 ref.x * ax.y - ref.y * ax.x};
        const double ul2 = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        if (ul2 < 1e-12) return {bolt.origin};  // tamamen dejenere
        u = Vec3{u.x / ul2, u.y / ul2, u.z / ul2};
    } else {
        u = Vec3{u.x / ul, u.y / ul, u.z / ul};
    }
    // y = axis x u (sag-el).
    const Vec3 v = Vec3{ax.y * u.z - ax.z * u.y, ax.z * u.x - ax.x * u.z,
                        ax.x * u.y - ax.y * u.x};

    out.reserve(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny));
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const double ox = bolt.origin.x + i * bolt.spacingX * u.x +
                              j * bolt.spacingY * v.x;
            const double oy = bolt.origin.y + i * bolt.spacingX * u.y +
                              j * bolt.spacingY * v.y;
            const double oz = bolt.origin.z + i * bolt.spacingX * u.z +
                              j * bolt.spacingY * v.z;
            out.push_back(Vec3{ox, oy, oz});
        }
    return out;
}

std::vector<CutModifier> boltGroupHoleModifiers(const BoltGroup& bolt) noexcept {
    std::vector<CutModifier> out;
    const std::vector<Vec3> origins = boltGroupBoltOrigins(bolt);
    out.reserve(origins.size() * bolt.parts.size());
    for (const Vec3& o : origins) {
        for (EntityId part : bolt.parts) {
            if (part == kInvalidEntityId) continue;
            out.push_back(makeHoleCutModifier(part, o, bolt.axis,
                                              bolt.params.holeDiameter));
        }
    }
    return out;
}

bool isBoltAxisValid(const Vec3& axis) noexcept {
    return std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z) > 1e-12;
}

const std::vector<double>& standardBoltDiameters() noexcept {
    static const std::vector<double> kDiameters{12.0, 16.0, 20.0, 24.0, 27.0, 30.0};
    return kDiameters;
}

double standardBoltHoleDiameter(double nominalDiameter) noexcept {
    const double d = nominalDiameter;
    // Katalog eslesmesi (toleranssiz; nominal cap tam verilir).
    if (std::abs(d - 12.0) < 1e-9) return 14.0;
    if (std::abs(d - 16.0) < 1e-9) return 18.0;
    if (std::abs(d - 20.0) < 1e-9) return 22.0;
    if (std::abs(d - 24.0) < 1e-9) return 26.0;
    if (std::abs(d - 27.0) < 1e-9) return 30.0;
    if (std::abs(d - 30.0) < 1e-9) return 33.0;
    return d + 2.0;  // katalog disi: nominal + 2 mm
}

bool isBoltGradeValid(const std::string& grade) noexcept {
    if (grade.empty()) return true;
    return grade == "8.8" || grade == "10.9";
}

} // namespace mm
