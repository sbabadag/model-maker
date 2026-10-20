// MODEL MAKER BRIDGE — paket okuyucu + dogrulayici (uygulama).
// nlohmann/json (tek baslik) kullanir; OCCT'siz, Linux OCC-off ctest'te kosar.

#include "model_maker/bridge_package.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace mm {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr long long kMaxFileBytes = 32LL * 1024 * 1024;
constexpr long long kMaxTotalBytes = 256LL * 1024 * 1024;

bool isVec3(const json& j) {
    return j.is_object() && j.contains("x") && j.contains("y") && j.contains("z") &&
           j["x"].is_number() && j["y"].is_number() && j["z"].is_number();
}

BridgeVec3 toVec3(const json& j) {
    BridgeVec3 v;
    if (j.is_object()) {
        if (j.contains("x") && j["x"].is_number()) v.x = j["x"].get<double>();
        if (j.contains("y") && j["y"].is_number()) v.y = j["y"].get<double>();
        if (j.contains("z") && j["z"].is_number()) v.z = j["z"].get<double>();
    }
    return v;
}

BridgeParamValue parseValue(const json& v) {
    if (v.is_null()) return std::monostate{};
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number_unsigned()) return static_cast<int>(v.get<std::uint64_t>());
    if (v.is_number_float()) return v.get<double>();
    if (v.is_string()) return v.get<std::string>();
    return std::monostate{};
}

std::string str(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return {};
    return j[key].get<std::string>();
}

int64_t integer(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number_integer()) return 0;
    return j[key].get<int64_t>();
}

std::vector<std::string> strList(const json& j, const char* key) {
    std::vector<std::string> out;
    if (!j.is_object() || !j.contains(key) || !j[key].is_array()) return out;
    for (const auto& e : j[key])
        if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

bool readJsonFile(const fs::path& p, json& out) {
    std::ifstream in(p);
    if (!in) return false;
    try {
        in >> out;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

bool isSafeBridgeRelativePath(const std::string& p) noexcept {
    if (p.empty()) return false;
    if (p.front() == '/' || p.front() == '\\') return false;
    if (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':')
        return false;  // windows surucu
    // tek tek bilesenler: bos, "." veya ".." yasak
    std::string cur;
    for (char c : p) {
        if (c == '/' || c == '\\') {
            if (cur.empty() || cur == "." || cur == "..") return false;
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (cur.empty() || cur == "." || cur == "..") return false;
    return true;
}

bool readBridgePackage(const std::string& dir, BridgePackage& out,
                       std::vector<std::string>& errors) {
    fs::path root(dir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        errors.push_back("paket klasoru yok: " + dir);
        return false;
    }

    // Toplam boyut siniri.
    long long total = 0;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file()) {
            long long len = static_cast<long long>(it->file_size(ec));
            if (ec) continue;
            total += len;
            if (len > kMaxFileBytes)
                errors.push_back("dosya cok buyuk: " + it->path().string());
        }
    }
    if (total > kMaxTotalBytes)
        errors.push_back("paket toplam boyutu cok buyuk: " + std::to_string(total));

    json m, comp, params, objects, diag;
    bool haveM = readJsonFile(root / "manifest.json", m);
    bool haveC = readJsonFile(root / "component.json", comp);
    bool haveP = readJsonFile(root / "parameters.json", params);
    bool haveO = readJsonFile(root / "objects.json", objects);
    bool haveD = readJsonFile(root / "diagnostics.json", diag);

    if (!haveM) errors.push_back("manifest.json okunamadi");
    if (!haveC) errors.push_back("component.json okunamadi");
    if (!haveP) errors.push_back("parameters.json okunamadi");
    if (!haveO) errors.push_back("objects.json okunamadi");
    if (!haveD) errors.push_back("diagnostics.json okunamadi");
    if (!haveM || !haveC || !haveP || !haveO || !haveD) return false;

    // --- manifest ---
    if (!m.is_object()) { errors.push_back("manifest nesne degil"); return false; }
    out.manifest.formatVersion = str(m, "formatVersion");
    out.manifest.sourceVersion = str(m, "sourceVersion");
    out.manifest.sourceComponentType = str(m, "sourceComponentType");
    out.manifest.sourceComponentNumber = static_cast<int>(integer(m, "sourceComponentNumber"));
    out.manifest.sourceComponentName = str(m, "sourceComponentName");
    out.manifest.sourceModelIdentifier = str(m, "sourceModelIdentifier");
    out.manifest.exportTimestamp = str(m, "exportTimestamp");
    out.manifest.coordinateSystem = str(m, "coordinateSystem");
    out.manifest.lengthUnit = str(m, "lengthUnit");
    out.manifest.componentIdentifier = str(m, "componentIdentifier");
    out.manifest.exportedObjectCount = static_cast<int>(integer(m, "exportedObjectCount"));
    out.manifest.knownLimitations = strList(m, "knownLimitations");

    if (out.manifest.formatVersion != "1.0")
        errors.push_back("manifest.formatVersion 1.0 olmali");
    if (out.manifest.componentIdentifier.empty())
        errors.push_back("manifest.componentIdentifier bos");
    if (out.manifest.sourceComponentType.empty())
        errors.push_back("manifest.sourceComponentType bos");

    // --- component ---
    if (!comp.is_object()) { errors.push_back("component nesne degil"); return false; }
    out.component.sourceType = str(comp, "sourceType");
    out.component.name = str(comp, "name");
    out.component.number = static_cast<int>(integer(comp, "number"));
    out.component.guid = str(comp, "guid");
    if (comp.contains("coordinateSystem") && comp["coordinateSystem"].is_object()) {
        const auto& cs = comp["coordinateSystem"];
        out.component.coordinateSystem.origin = toVec3(cs.value("origin", json::object()));
        out.component.coordinateSystem.xAxis = toVec3(cs.value("xAxis", json::object()));
        out.component.coordinateSystem.yAxis = toVec3(cs.value("yAxis", json::object()));
        out.component.coordinateSystem.zAxis = toVec3(cs.value("zAxis", json::object()));
    }
    if (comp.contains("primaryMember") && comp["primaryMember"].is_object()) {
        const auto& pm = comp["primaryMember"];
        out.component.primaryMember.guid = str(pm, "guid");
        out.component.primaryMember.profile = str(pm, "profile");
        out.component.primaryMember.material = str(pm, "material");
        if (pm.contains("startPoint") && isVec3(pm["startPoint"]))
            out.component.primaryMember.startPoint = toVec3(pm["startPoint"]);
        if (pm.contains("endPoint") && isVec3(pm["endPoint"]))
            out.component.primaryMember.endPoint = toVec3(pm["endPoint"]);
    }
    if (comp.contains("secondaryMembers") && comp["secondaryMembers"].is_array()) {
        for (const auto& sm : comp["secondaryMembers"]) {
            if (!sm.is_object()) continue;
            BridgeMemberRef ref;
            ref.guid = str(sm, "guid");
            ref.profile = str(sm, "profile");
            ref.material = str(sm, "material");
            if (sm.contains("startPoint") && isVec3(sm["startPoint"]))
                ref.startPoint = toVec3(sm["startPoint"]);
            if (sm.contains("endPoint") && isVec3(sm["endPoint"]))
                ref.endPoint = toVec3(sm["endPoint"]);
            out.component.secondaryMembers.push_back(std::move(ref));
        }
    }
    if (out.component.guid.empty()) errors.push_back("component.guid bos");

    // --- parameters ---
    if (params.is_array()) {
        std::set<std::string> seen;
        for (const auto& p : params) {
            if (!p.is_object()) continue;
            BridgeParameter bp;
            bp.name = str(p, "name");
            bp.type = str(p, "type");
            bp.units = str(p, "units");
            bp.source = str(p, "source");
            bp.readable = str(p, "readable");
            bp.confidence = str(p, "confidence");
            if (p.contains("value")) bp.value = parseValue(p["value"]);
            if (bp.name.empty()) { errors.push_back("parameter adi bos"); continue; }
            if (!seen.insert(bp.name).second) errors.push_back("parameter tekrarli: " + bp.name);
            out.parameters.push_back(std::move(bp));
        }
    } else {
        errors.push_back("parameters liste degil");
    }

    // --- objects ---
    if (objects.is_array()) {
        std::set<std::string> seen;
        for (const auto& o : objects) {
            if (!o.is_object()) continue;
            BridgeObject bo;
            bo.sourceGUID = str(o, "sourceGUID");
            bo.kind = str(o, "kind");
            bo.name = str(o, "name");
            bo.role = str(o, "role");
            bo.profile = str(o, "profile");
            bo.material = str(o, "material");
            bo.parentSourceGUID = str(o, "parentSourceGUID");
            bo.mapped = o.value("mapped", false);
            bo.mappedKind = str(o, "mappedKind");
            bo.geometryFile = str(o, "geometryFile");
            bo.unsupportedReason = str(o, "unsupportedReason");
            if (o.contains("identifierId") && o["identifierId"].is_number_integer())
                bo.identifierId = static_cast<int>(o["identifierId"].get<int64_t>());
            if (bo.sourceGUID.empty()) { errors.push_back("object sourceGUID bos"); continue; }
            if (!seen.insert(bo.sourceGUID).second)
                errors.push_back("object sourceGUID tekrarli: " + bo.sourceGUID);

            // geometry dosyasi: guvenli yol + var olmali + bbox yukle.
            if (!bo.geometryFile.empty()) {
                if (!isSafeBridgeRelativePath(bo.geometryFile)) {
                    errors.push_back("geometryFile guvenli degil: " + bo.geometryFile);
                } else {
                    fs::path gp = root / bo.geometryFile;
                    json g;
                    if (!readJsonFile(gp, g)) {
                        errors.push_back("geometry dosyasi okunamadi: " + bo.geometryFile);
                    } else if (g.is_object() && g.contains("type") && g["type"] == "bbox" &&
                               g.contains("bbox") && g["bbox"].is_object() &&
                               isVec3(g["bbox"].value("min", json())) &&
                               isVec3(g["bbox"].value("max", json()))) {
                        BridgeBBox bb;
                        bb.min = toVec3(g["bbox"]["min"]);
                        bb.max = toVec3(g["bbox"]["max"]);
                        bo.bbox = bb;
                    }
                }
            }
            out.objects.push_back(std::move(bo));
        }
    } else {
        errors.push_back("objects liste degil");
    }

    // --- diagnostics ---
    out.knownLimitations = strList(diag, "knownLimitations");
    out.inaccessibleObjects = strList(diag, "inaccessibleObjects");
    out.unsupportedFeatures = strList(diag, "unsupportedFeatures");
    out.warnings = strList(diag, "warnings");

    return errors.empty();
}

}  // namespace mm
