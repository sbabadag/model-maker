// Opt-in DSTV-backed native Tekla 146 preparation.
// The original BridgePackage remains unmodified; no OCC/Document actions here.
#include "model_maker/bridge_nc146.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

namespace mm {
namespace {

struct NcHole { double u = 0.0, v = 0.0, diameter = 0.0; };
struct NcPlate {
    std::vector<std::string> header;
    std::vector<NcHole> holes;
    double length = 0.0, width = 0.0, thickness = 0.0;
    int contours = 0;
    bool innerContour = false;
};

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}
bool number(const std::string& s, double& v) {
    try {
        std::size_t used = 0;
        v = std::stod(s, &used);
        return used == s.size() && std::isfinite(v);
    } catch (...) { return false; }
}
bool close(double a, double b, double tol = 0.25) {
    return std::isfinite(a) && std::isfinite(b) && std::fabs(a-b) <= tol;
}
BridgeVec3 add(const BridgeVec3& a, const BridgeVec3& b, double scale) {
    return {a.x+b.x*scale, a.y+b.y*scale, a.z+b.z*scale};
}
double len(const BridgeVec3& a) {
    return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);
}
double dot(const BridgeVec3& a, const BridgeVec3& b) {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
BridgeVec3 cross(const BridgeVec3& a, const BridgeVec3& b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}

bool parseNc(const std::filesystem::path& path, NcPlate& out, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) ||
        std::filesystem::file_size(path, ec) > 4 * 1024 * 1024) {
        error = "NC1 file missing or too large"; return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "NC1 could not be opened"; return false; }

    const std::regex hole(
        R"(^\s*v\s*([+-]?\d+(?:\.\d+)?)\s*s\s*([+-]?\d+(?:\.\d+)?)\s+(\d+(?:\.\d+)?)\s*$)",
        std::regex::icase);
    std::string line, state;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t == "ST" || t == "AK" || t == "BO" || t == "IK" || t == "EN" ||
            t == "KO" || t == "PU" || t == "SI" || t == "KA") {
            state = t;
            if (state == "IK") out.innerContour = true;
            if (state == "EN") break;
            continue;
        }
        if (t.empty()) continue;
        if (state == "ST") out.header.push_back(t);
        else if (state == "AK") ++out.contours;
        else if (state == "BO") {
            std::smatch match;
            if (!std::regex_match(t, match, hole)) {
                error = "NC1 contains non-round/unsupported BO drilling syntax: " + t;
                return false;
            }
            NcHole h;
            if (!number(match[1], h.u) || !number(match[2], h.v) ||
                !number(match[3], h.diameter) || h.diameter <= 0) {
                error = "NC1 invalid hole coordinates or diameter"; return false;
            }
            out.holes.push_back(h);
        } else if (state == "IK" || state == "KO" || state == "PU" ||
                   state == "SI" || state == "KA") {
            error = "NC1 has extra machining/inner contour; not supported in native 146";
            return false;
        }
    }
    const auto b = std::find(out.header.begin(), out.header.end(), "B");
    if (state != "EN" || b == out.header.end() ||
        std::distance(b, out.header.end()) < 4) {
        error = "NC1 incomplete ST/B/AK/BO/EN plate definition"; return false;
    }
    if (!number(*(b+1), out.length) || !number(*(b+2), out.width) ||
        !number(*(b+3), out.thickness) ||
        out.length <= 0 || out.width <= 0 || out.thickness <= 0) {
        error = "NC1 plate dimensions invalid"; return false;
    }
    if (out.innerContour || out.contours != 5 ||
        out.holes.size() != 6) {
        error = "Native 146 expects rectangular AK and precisely six NC BO holes";
        return false;
    }
    return true;
}
} // namespace

BridgeNc146Result prepareBridge146FromNc1(
    const BridgePackage& source, const std::filesystem::path& nc1Path) {
    BridgeNc146Result result;
    const auto fail = [&](const std::string& reason) {
        result.error = reason; return result;
    };

    const int number146 = source.manifest.sourceComponentNumber
                              ? source.manifest.sourceComponentNumber
                              : source.component.number;
    if (number146 != 146) return fail("Only Tekla connection 146 is supported");

    const BridgeObject* plate = nullptr;
    const BridgeObject* group = nullptr;
    for (const auto& obj : source.objects) {
        if (obj.kind == "ContourPlate") {
            if (plate) return fail("Multiple plate targets; ambiguous");
            plate = &obj;
        }
        if (obj.kind == "BoltGroup") {
            if (group) return fail("Multiple bolt groups; ambiguous");
            group = &obj;
        }
    }
    if (!plate || !plate->bbox || !plate->plateDstvFrame ||
        !group || !group->boltGroup)
        return fail("Source plate bbox/DSTV coordinate frame or bolt group missing");
    if (plate->sourceGUID.empty() ||
        group->boltGroup->partToBoltTo != plate->sourceGUID)
        return fail("Bolt-to-plate source GUID relationship mismatched");

    NcPlate nc;
    std::string error;
    if (!parseNc(nc1Path, nc, error)) return fail(error);

    // Strictly validate the NC material/section and part identity when known.
    if (std::find(nc.header.begin(), nc.header.end(), plate->profile) == nc.header.end() ||
        std::find(nc.header.begin(), nc.header.end(), plate->material) == nc.header.end())
        return fail("NC1 material/profile mismatch with source plate");
    if (!plate->platePartMark.empty() &&
        std::find(nc.header.begin(), nc.header.end(), plate->platePartMark) == nc.header.end())
        return fail("NC1 part mark does not match Tekla source plate");

    const auto& bb = *plate->bbox;
    const std::array<double,3> measured = {bb.max.x-bb.min.x,
                                           bb.max.y-bb.min.y,
                                           bb.max.z-bb.min.z};
    auto sorted = measured;
    std::sort(sorted.begin(), sorted.end());
    std::array<double,3> ncDims = {nc.length,nc.width,nc.thickness};
    std::sort(ncDims.begin(), ncDims.end());
    if (measured[0] <= 0 || measured[1] <= 0 || measured[2] <= 0 ||
        !close(sorted[0],ncDims[0]) ||
        !close(sorted[1],ncDims[1]) ||
        !close(sorted[2],ncDims[2]))
        return fail("NC1 geometry does not match measured plate bbox");

    // Uses source Part.GetDSTVCoordinateSystem(), never bbox-derived face axes.
    const BridgeTransform& frame = *plate->plateDstvFrame;
    if (!frame.valid || frame.hasMatrix ||
        !close(len(frame.xAxis),1.0,1e-5) ||
        !close(len(frame.yAxis),1.0,1e-5) ||
        !close(dot(frame.xAxis,frame.yAxis),0.0,1e-5))
        return fail("Invalid or non-orthonormal observed Tekla DSTV frame");
    const BridgeVec3 normal = cross(frame.xAxis,frame.yAxis);
    const std::size_t minAxis =
        static_cast<std::size_t>(std::min_element(measured.begin(),measured.end())-measured.begin());
    const double nz = minAxis == 0 ? normal.x : minAxis == 1 ? normal.y : normal.z;
    if (std::fabs(nz) < 0.999)
        return fail("Measured DSTV face normal does not follow plate thickness");

    const auto& bolts = group->boltGroup->bolts;
    if (bolts.size() != nc.holes.size())
        return fail("NC1 hole count differs from Tekla BoltPositions");

    // Verify 1:1 NC holes against actual Tekla positions on both independent
    // in-plane world directions. We do not use/guess source bolt depth X=0.
    std::vector<bool> used(bolts.size(),false);
    std::vector<BridgeVec3> finalCenters;
    const BridgeVec3 center{
        0.5*(bb.min.x+bb.max.x),
        0.5*(bb.min.y+bb.max.y),
        0.5*(bb.min.z+bb.max.z)
    };
    const auto transverseDistance = [minAxis](BridgeVec3 a, BridgeVec3 b) {
        if (minAxis == 0) a.x=b.x;
        else if (minAxis == 1) a.y=b.y;
        else a.z=b.z;
        return len({a.x-b.x,a.y-b.y,a.z-b.z});
    };
    for (const auto& h : nc.holes) {
        const BridgeVec3 p =
            add(add(frame.origin,frame.xAxis,h.u),frame.yAxis,h.v);
        BridgeVec3 centre = p;
        if (minAxis == 0) centre.x = center.x;
        else if (minAxis == 1) centre.y = center.y;
        else centre.z = center.z;
        if (centre.x < bb.min.x-0.5 || centre.x > bb.max.x+0.5 ||
            centre.y < bb.min.y-0.5 || centre.y > bb.max.y+0.5 ||
            centre.z < bb.min.z-0.5 || centre.z > bb.max.z+0.5)
            return fail("NC1 hole projects outside measured Tekla plate bbox");
        // Physical Ø22 hole must fit inside all plate transverse boundaries.
        const double r = 0.5*h.diameter;
        if ((minAxis != 0 && (centre.x-r < bb.min.x || centre.x+r > bb.max.x)) ||
            (minAxis != 1 && (centre.y-r < bb.min.y || centre.y+r > bb.max.y)) ||
            (minAxis != 2 && (centre.z-r < bb.min.z || centre.z+r > bb.max.z)))
            return fail("NC1 hole cuts past plate edge");
        std::size_t matched = bolts.size();
        for (std::size_t i=0; i<bolts.size(); ++i) {
            if (used[i]) continue;
            if (transverseDistance(centre,bolts[i].position) < 0.5) {
                matched=i; break;
            }
        }
        if (matched == bolts.size())
            return fail("NC1 hole centre does not match a unique Tekla BoltPosition");
        if (!close(h.diameter,nc.holes.front().diameter,0.01))
            return fail("Mixed hole diameters cannot be one editable native bolt group");
        used[matched]=true;
        finalCenters.push_back(centre);
    }

    // Promote only this independently checked *copy* as observed for
    // Stage3C drill guard and editable native 146 generator.
    result.prepared=source;
    for (auto& obj : result.prepared.objects) {
        if (obj.kind != "BoltGroup" || !obj.boltGroup) continue;
        auto& g=*obj.boltGroup;
        const char* ax = minAxis==0 ? "x" : minAxis==1 ? "y" : "z";
        g.axis=ax;
        g.axisSource="measured";
        g.direction=BridgeVec3{minAxis==0?1.0:0.0,
                               minAxis==1?1.0:0.0,
                               minAxis==2?1.0:0.0};
        // Preserve source bolt order/identity, matching in-plane coordinates.
        for (auto& bolt : g.bolts) {
            if (minAxis==0) bolt.position.x=center.x;
            else if (minAxis==1) bolt.position.y=center.y;
            else bolt.position.z=center.z;
            bolt.axis=ax;
            bolt.axisSource="measured";
            bolt.holeDiameter=nc.holes.front().diameter;
            bolt.holeDiameterSource="measured";
        }
    }
    result.ok=true;
    result.holes=static_cast<int>(nc.holes.size());
    result.diameter=nc.holes.front().diameter;
    return result;
}
} // namespace mm
