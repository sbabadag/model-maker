#include "model_maker/bolt.hpp"

#include <algorithm>
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
                                const Vec3& axis, double holeDiameter,
                                double depth) noexcept {
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
    mod.depth = depth;
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

double boltGroupSpanX(const BoltGroup& bolt) noexcept {
    const int n = bolt.countX > 0 ? bolt.countX : 1;
    if (n <= 1) return 0.0;
    if (static_cast<int>(bolt.spacingXs.size()) == n - 1) {
        double s = 0.0;
        for (const double v : bolt.spacingXs) s += v;
        return s;
    }
    return (n - 1) * bolt.spacingX;
}

double boltGroupSpanY(const BoltGroup& bolt) noexcept {
    const int n = bolt.countY > 0 ? bolt.countY : 1;
    if (n <= 1) return 0.0;
    if (static_cast<int>(bolt.spacingYs.size()) == n - 1) {
        double s = 0.0;
        for (const double v : bolt.spacingYs) s += v;
        return s;
    }
    return (n - 1) * bolt.spacingY;
}

Vec3 boltGroupCenteredOrigin(const BoltGroup& bolt, const Vec3& boundsCenter,
                             bool centerX, bool centerY) noexcept {
    const BoltFrame f = boltFrame(bolt);
    // Grup merkezinin origin'e gore yerel x/y uzakligi (pattern kose ortasi).
    const double cx = 0.5 * boltGroupSpanX(bolt);
    const double cy = 0.5 * boltGroupSpanY(bolt);
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
    // Kumulatif x/y ofsetleri (degisken aralik destegi). Sabit aralik durumunda
    // spacingX/Y'nin kumulatif toplami i*spacingX'e esittir (ayni sonuc).
    const bool varX = static_cast<int>(bolt.spacingXs.size()) == nx - 1 && nx > 1;
    const bool varY = static_cast<int>(bolt.spacingYs.size()) == ny - 1 && ny > 1;
    std::vector<double> cumX(static_cast<std::size_t>(nx), 0.0);
    std::vector<double> cumY(static_cast<std::size_t>(ny), 0.0);
    for (int i = 1; i < nx; ++i)
        cumX[static_cast<std::size_t>(i)] =
            cumX[static_cast<std::size_t>(i - 1)] +
            (varX ? bolt.spacingXs[static_cast<std::size_t>(i - 1)] : bolt.spacingX);
    for (int j = 1; j < ny; ++j)
        cumY[static_cast<std::size_t>(j)] =
            cumY[static_cast<std::size_t>(j - 1)] +
            (varY ? bolt.spacingYs[static_cast<std::size_t>(j - 1)] : bolt.spacingY);
    out.reserve(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny));
    for (int j = 0; j < ny; ++j) {
        const int rowFromTop = ny - j;               // 1 = ust sira
        const bool oddRow = (rowFromTop % 2) == 1;
        for (int i = 0; i < nx; ++i) {
            const int boltNo = j * nx + i + 1;  // 1-based (j=0 en alt, i soldan)
            bool deleted = false;
            for (const int d : bolt.deletedBolts)
                if (d == boltNo) {
                    deleted = true;
                    break;
                }
            if (deleted) continue;
            double cx = cumX[static_cast<std::size_t>(i)];
            double cy = cumY[static_cast<std::size_t>(j)];
            switch (bolt.staggerMode) {
                case 1:
                case 2: {
                    const bool shiftedRow = (bolt.staggerMode == 1) ? oddRow : !oddRow;
                    if (shiftedRow && nx > 1) {
                        if (i == nx - 1) continue;  // bir kolon eksik
                        cx = 0.5 * (cx + cumX[static_cast<std::size_t>(i + 1)]);
                    }
                    break;
                }
                case 3:
                    if (oddRow) cx += bolt.staggerOffset;
                    break;
                case 4:
                    if (!oddRow) cx += bolt.staggerOffset;
                    break;
                case 5:
                    if ((i % 2) == 1) cy += bolt.staggerOffset;
                    break;
                default:
                    break;
            }
            const double ox = bolt.origin.x + cx * f.u.x + cy * f.v.x;
            const double oy = bolt.origin.y + cx * f.u.y + cy * f.v.y;
            const double oz = bolt.origin.z + cx * f.u.z + cy * f.v.z;
            out.push_back(Vec3{ox, oy, oz});
        }
    }
    return out;
}

void resolveHoleShapeParts(BoltGroup& bolt, const std::vector<EntityId>& ownedPlates) noexcept {
    bolt.shapeParts.clear();
    if (bolt.holeShapeTarget <= 0) return;
    for (const EntityId p : bolt.parts) {
        if (p == kInvalidEntityId) continue;
        const bool isPlate =
            std::find(ownedPlates.begin(), ownedPlates.end(), p) != ownedPlates.end();
        const bool take = (bolt.holeShapeTarget == 3) ||
                          (bolt.holeShapeTarget == 1 && isPlate) ||
                          (bolt.holeShapeTarget == 2 && !isPlate);
        if (take) bolt.shapeParts.push_back(p);
    }
}

std::vector<CutModifier> boltGroupHoleModifiers(const BoltGroup& bolt) noexcept {
    std::vector<CutModifier> out;
    if (!bolt.createHoles) return out;  // Tekla "No hole"
    const std::vector<Vec3> origins = boltGroupBoltOrigins(bolt);
    out.reserve(origins.size() * bolt.parts.size());
    for (const Vec3& o : origins) {
        for (EntityId part : bolt.parts) {
            if (part == kInvalidEntityId) continue;
            const bool shaped =
                bolt.holeShapeTarget > 0 &&
                std::find(bolt.shapeParts.begin(), bolt.shapeParts.end(), part) !=
                    bolt.shapeParts.end();
            const double dia = (shaped && bolt.shapeHoleDiameter > 0.0)
                                   ? bolt.shapeHoleDiameter
                                   : bolt.params.holeDiameter;
            CutModifier m = makeHoleCutModifier(part, o, bolt.axis, dia, bolt.holeDepth);
            if (shaped && (bolt.slotAlong > 0.0 || bolt.slotAcross > 0.0)) {
                m.slotAlong = bolt.slotAlong;
                m.slotAcross = bolt.slotAcross;
                m.slotDir = bolt.slotDir;
            }
            out.push_back(std::move(m));
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
