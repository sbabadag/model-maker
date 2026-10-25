#pragma once
// Tekla 146: opt-in native component creation using an independently supplied
// DSTV/NC1 file. The original source BridgePackage is never modified.

#include "model_maker/bridge_package.hpp"

#include <filesystem>
#include <string>

namespace mm {
struct BridgeNc146Result {
    bool ok = false;
    std::string error;
    BridgePackage prepared; // source copy; hole geometry inserted only after checks
    int holes = 0;
    double diameter = 0.0;
};

// Checks plate GUID, part mark if present, DSTV frame (required), dimensions,
// material/profile, all NC holes and all source bolt positions. For validated
// 146 only, projects the NC positions onto the measured plate centre plane and
// sets measured bolt axis/hole diameter in a COPY for the native generator.
// No Document/OCC changes occur here; users must separately select real members.
BridgeNc146Result prepareBridge146FromNc1(
    const BridgePackage& source, const std::filesystem::path& nc1Path);
} // namespace mm
