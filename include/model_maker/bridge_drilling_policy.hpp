#pragma once
// Fail-closed policy for source-derived Bridge bolt hole creation.
// This policy is OCC-independent so it can be regression-tested without OCCT.
// Inferred axes, inferred hole sizes, guessed target parts, and coordinates
// re-centered to compensate for source-frame mismatches are NOT authoritative.

#include "model_maker/bridge_package.hpp"

#include <cmath>
#include <string>

namespace mm {

struct BridgeDrillAuthorization {
    bool allowed = false;
    std::string reason;
};

// Authorize physical *automatic* hole generation, not reference display.
// Only source-observed axis AND hole geometry with a matching source target
// can pass. Source fields are untrusted; geometry must be internally consistent.
inline BridgeDrillAuthorization authorizeBridgeAutoDrill(
    const BridgeBoltGroupInfo& g, const BridgeBBox& plate,
    const std::string& targetSourceGUID) {

    const auto reject = [](const char* reason) {
        return BridgeDrillAuthorization{false, reason};
    };
    const auto observed = [](const std::string& source) {
        return source == "measured" || source == "observed";
    };
    const auto validRange = [](double lo, double hi) {
        return std::isfinite(lo) && std::isfinite(hi) && hi > lo;
    };
    if (targetSourceGUID.empty() || g.partToBoltTo.empty() ||
        g.partToBoltTo != targetSourceGUID)
        return reject("bolt target GUID unresolved or does not match imported plate");
    if (!validRange(plate.min.x, plate.max.x) ||
        !validRange(plate.min.y, plate.max.y) ||
        !validRange(plate.min.z, plate.max.z))
        return reject("invalid plate bounds");

    // A direction inferred from the thinnest BBox dimension does NOT count.
    if (g.axis.empty() || !observed(g.axisSource))
        return reject("bolt axis not independently observed; automatic drilling disabled");

    const bool axisX = g.axis == "x" || g.axis == "+x" || g.axis == "-x";
    const bool axisY = g.axis == "y" || g.axis == "+y" || g.axis == "-y";
    const bool axisZ = g.axis == "z" || g.axis == "+z" || g.axis == "-z";
    if (!axisX && !axisY && !axisZ)
        return reject("bolt axis value unsupported for verified automatic drilling");
    if (g.bolts.empty())
        return reject("no measured bolt positions");

    constexpr double tol = 1e-6;
    for (const BridgeBolt& b : g.bolts) {
        const double x = b.position.x, y = b.position.y, z = b.position.z;
        const double r = b.holeDiameter * 0.5;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return reject("non-finite bolt coordinates");
        if (!std::isfinite(r) || r <= 0.0 || !observed(b.holeDiameterSource))
            return reject("hole diameter is inferred or unresolved; automatic drilling disabled");
        // Do NOT silently shift the source bolt along its drilling axis into
        // the plate. A source X=0 with plate X=4.25..14.25 is unverified.
        if (x < plate.min.x - tol || x > plate.max.x + tol ||
            y < plate.min.y - tol || y > plate.max.y + tol ||
            z < plate.min.z - tol || z > plate.max.z + tol)
            return reject("source bolt coordinates do not intersect source plate bounds");
        // Full circular hole must fit in the two directions transverse to axis.
        if ((axisX && (y - r < plate.min.y - tol || y + r > plate.max.y + tol ||
                       z - r < plate.min.z - tol || z + r > plate.max.z + tol)) ||
            (axisY && (x - r < plate.min.x - tol || x + r > plate.max.x + tol ||
                       z - r < plate.min.z - tol || z + r > plate.max.z + tol)) ||
            (axisZ && (x - r < plate.min.x - tol || x + r > plate.max.x + tol ||
                       y - r < plate.min.y - tol || y + r > plate.max.y + tol)))
            return reject("source bolt hole overlaps the plate outside edge");
    }
    return BridgeDrillAuthorization{true, "observed axis, target, positions, and hole diameters"};
}

} // namespace mm
