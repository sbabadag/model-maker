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

namespace {

// Yerel ortogonal cerceve: ax = birim eksen, u = eksene dik birim x yonu,
// v = ax x u (sag-el). Her zaman gecerli bir cerceve dondurur; dejenere
// giriste guvenli varsayilanlar kullanilir (axis->+Z, xDir->+X, eksene paralel
// xDir -> keyfi dik taban).
struct BoltFrame {
    Vec3 ax{0.0, 0.0, 1.0};
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};
};

BoltFrame boltFrame(const BoltGroup& bolt) noexcept {
    BoltFrame f;
    const double axl = std::sqrt(bolt.axis.x * bolt.axis.x + bolt.axis.y * bolt.axis.y +
                                 bolt.axis.z * bolt.axis.z);
    f.ax = axl > 1e-12 ? Vec3{bolt.axis.x / axl, bolt.axis.y / axl, bolt.axis.z / axl}
                       : Vec3{0.0, 0.0, 1.0};
    const double xl = std::sqrt(bolt.xDir.x * bolt.xDir.x + bolt.xDir.y * bolt.xDir.y +
                                bolt.xDir.z * bolt.xDir.z);
    Vec3 ux = xl > 1e-12 ? Vec3{bolt.xDir.x / xl, bolt.xDir.y / xl, bolt.xDir.z / xl}
                         : Vec3{1.0, 0.0, 0.0};
    // xDir eksene dik olmali; dik degilse (dosya/el giris) Gram-Schmidt ile duzelt.
    const double dot = ux.x * f.ax.x + ux.y * f.ax.y + ux.z * f.ax.z;
    Vec3 u = ux - Vec3{f.ax.x * dot, f.ax.y * dot, f.ax.z * dot};
    const double ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    if (ul < 1e-12) {
        // xDir eksene paralel; keyfi dik bir taban kur.
        Vec3 ref = (std::abs(f.ax.z) > 0.9) ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 0.0, 1.0};
        u = Vec3{ref.y * f.ax.z - ref.z * f.ax.y, ref.z * f.ax.x - ref.x * f.ax.z,
                 ref.x * f.ax.y - ref.y * f.ax.x};
        const double ul2 = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        u = ul2 < 1e-12 ? Vec3{1.0, 0.0, 0.0} : Vec3{u.x / ul2, u.y / ul2, u.z / ul2};
    } else {
        u = Vec3{u.x / ul, u.y / ul, u.z / ul};
    }
    f.u = u;
    f.v = Vec3{f.ax.y * u.z - f.ax.z * u.y, f.ax.z * u.x - f.ax.x * u.z,
               f.ax.x * u.y - f.ax.y * u.x};
    return f;
}

} // namespace

Vec3 boltGroupCenteredOrigin(const BoltGroup& bolt, const Vec3& boundsCenter,
                             bool centerX, bool centerY) noexcept {
    const BoltFrame f = boltFrame(bolt);
    const int nx = bolt.countX > 0 ? bolt.countX : 1;
    const int ny = bolt.countY > 0 ? bolt.countY : 1;
    // Grup merkezinin origin'e gore yerel x/y uzakligi (pattern kose ortasi).
    const double cx = (nx - 1) * 0.5 * bolt.spacingX;
    const double cy = (ny - 1) * 0.5 * bolt.spacingY;
    Vec3 origin = bolt.origin;
    if (centerX) {
        const Vec3 d = boundsCenter - origin;
        const double delta = (d.x * f.u.x + d.y * f.u.y + d.z * f.u.z) - cx;
        origin = Vec3{origin.x + delta * f.u.x, origin.y + delta * f.u.y,
                      origin.z + delta * f.u.z};
    }
    if (centerY) {
        const Vec3 d = boundsCenter - origin;
        const double delta = (d.x * f.v.x + d.y * f.v.y + d.z * f.v.z) - cy;
        origin = Vec3{origin.x + delta * f.v.x, origin.y + delta * f.v.y,
                      origin.z + delta * f.v.z};
    }
    return origin;
}

std::vector<Vec3> boltGroupBoltOrigins(const BoltGroup& bolt) noexcept {
    std::vector<Vec3> out;
    const int nx = bolt.countX > 0 ? bolt.countX : 1;
    const int ny = bolt.countY > 0 ? bolt.countY : 1;
    const BoltFrame f = boltFrame(bolt);
    out.reserve(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny));
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const int boltNo = j * nx + i + 1;  // 1-based (j=0 en alt, i soldan)
            bool deleted = false;
            for (const int d : bolt.deletedBolts)
                if (d == boltNo) {
                    deleted = true;
                    break;
                }
            if (deleted) continue;
            const double ox = bolt.origin.x + i * bolt.spacingX * f.u.x +
                              j * bolt.spacingY * f.v.x;
            const double oy = bolt.origin.y + i * bolt.spacingX * f.u.y +
                              j * bolt.spacingY * f.v.y;
            const double oz = bolt.origin.z + i * bolt.spacingX * f.u.z +
                              j * bolt.spacingY * f.v.z;
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
