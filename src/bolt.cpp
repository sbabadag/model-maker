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

std::vector<CutModifier> boltGroupHoleModifiers(const BoltGroup& bolt) noexcept {
    std::vector<CutModifier> out;
    out.reserve(bolt.parts.size());
    for (EntityId part : bolt.parts) {
        if (part == kInvalidEntityId) continue;
        out.push_back(makeHoleCutModifier(part, bolt.origin, bolt.axis,
                                          bolt.params.holeDiameter));
    }
    return out;
}

bool isBoltAxisValid(const Vec3& axis) noexcept {
    return std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z) > 1e-12;
}

} // namespace mm
