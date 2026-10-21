// MODEL MAKER BRIDGE — paket okuyucu + dogrulayici (uygulama).
// nlohmann/json (tek baslik) kullanir; OCCT'siz, Linux OCC-off ctest'te kosar.
// V3: relationships/operations/checksums + roleKey + mesh + fitting/boolean.

#include "model_maker/bridge_package.hpp"
#include "model_maker/sha256.hpp"

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

bool boolean(const json& j, const char* key, bool dflt = false) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_boolean()) return dflt;
    return j[key].get<bool>();
}

double number(const json& j, const char* key, double dflt = 0.0) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number()) return dflt;
    return j[key].get<double>();
}

std::vector<std::string> strList(const json& j, const char* key) {
    std::vector<std::string> out;
    if (!j.is_object() || !j.contains(key) || !j[key].is_array()) return out;
    for (const auto& e : j[key])
        if (e.is_string()) out.push_back(e.get<std::string>());
    return out;
}

// --- V2 alt-nesne ayristiricilari ------------------------------------------

BridgeBoltGroupInfo parseBoltGroup(const json& g) {
    BridgeBoltGroupInfo info;
    info.size = str(g, "size");
    if (g.contains("standardCode") && g["standardCode"].is_number_integer())
        info.standardCode = static_cast<int>(g["standardCode"].get<int64_t>());
    info.tolerance = number(g, "tolerance");
    info.slottedHoleX = number(g, "slottedHoleX");
    info.slottedHoleY = number(g, "slottedHoleY");
    info.partToBeBolted = str(g, "partToBeBolted");
    info.partToBoltTo = str(g, "partToBoltTo");
    info.axis = str(g, "axis");
    info.axisSource = str(g, "axisSource");
    if (g.contains("direction") && isVec3(g["direction"]))
        info.direction = toVec3(g["direction"]);
    info.boltType = str(g, "boltType");
    info.boltStandard = str(g, "boltStandard");
    if (g.contains("bolts") && g["bolts"].is_array()) {
        for (const auto& b : g["bolts"]) {
            if (!b.is_object()) continue;
            BridgeBolt bolt;
            if (b.contains("position") && isVec3(b["position"]))
                bolt.position = toVec3(b["position"]);
            bolt.nominalDiameter = number(b, "nominalDiameter");
            bolt.holeDiameter = number(b, "holeDiameter");
            bolt.holeDiameterSource = str(b, "holeDiameterSource");
            bolt.axis = str(b, "axis");
            bolt.axisSource = str(b, "axisSource");
            info.bolts.push_back(std::move(bolt));
        }
    }
    return info;
}

BridgeWeldInfo parseWeld(const json& w) {
    BridgeWeldInfo info;
    info.type = str(w, "type");
    info.size = number(w, "size");
    info.sizeAbove = number(w, "sizeAbove");
    info.sizeBelow = number(w, "sizeBelow");
    info.mainSourceGUID = str(w, "mainSourceGUID");
    info.secondarySourceGUID = str(w, "secondarySourceGUID");
    info.position = str(w, "position");
    return info;
}

BridgeTopology parseTopology(const json& t) {
    BridgeTopology topo;
    if (t.contains("faceCount") && t["faceCount"].is_number_integer())
        topo.faceCount = static_cast<int>(t["faceCount"].get<int64_t>());
    if (t.contains("loopCount") && t["loopCount"].is_number_integer())
        topo.loopCount = static_cast<int>(t["loopCount"].get<int64_t>());
    if (t.contains("faceVertexOccurrences") && t["faceVertexOccurrences"].is_number_integer())
        topo.faceVertexOccurrences =
            static_cast<int>(t["faceVertexOccurrences"].get<int64_t>());
    if (t.contains("holeLoops") && t["holeLoops"].is_number_integer())
        topo.holeLoops = static_cast<int>(t["holeLoops"].get<int64_t>());
    return topo;
}

// --- V3 alt-nesne ayristiricilari ------------------------------------------

std::optional<BridgePlane> parsePlane(const json& p) {
    if (!p.is_object() || !p.contains("origin") || !isVec3(p["origin"]) ||
        !p.contains("normal") || !isVec3(p["normal"]))
        return std::nullopt;
    BridgePlane pl;
    pl.origin = toVec3(p["origin"]);
    pl.normal = toVec3(p["normal"]);
    return pl;
}

BridgeFittingInfo parseFitting(const json& f) {
    BridgeFittingInfo info;
    if (f.contains("plane")) info.plane = parsePlane(f["plane"]);
    if (f.contains("normal") && isVec3(f["normal"])) info.normal = toVec3(f["normal"]);
    info.targetPartGUID = str(f, "targetPartGUID");
    info.keepSide = str(f, "keepSide");
    info.inferred = boolean(f, "inferred");
    return info;
}

BridgeBooleanPartInfo parseBooleanPart(const json& b) {
    BridgeBooleanPartInfo info;
    info.operationType = str(b, "operationType");
    info.targetPartGUID = str(b, "targetPartGUID");
    info.operandGUID = str(b, "operandGUID");
    info.inferred = boolean(b, "inferred");
    return info;
}

BridgeTransform parseTransform(const json& t) {
    BridgeTransform tr;
    if (!t.is_object()) return tr;
    if (t.contains("matrix") && t["matrix"].is_array()) {
        const auto& m = t["matrix"];
        if (m.size() == 4) {
            int idx = 0;
            bool allNum = true;
            for (const auto& row : m) {
                if (!row.is_array() || row.size() != 4) { allNum = false; break; }
                for (const auto& e : row) {
                    if (!e.is_number()) { allNum = false; break; }
                    tr.matrix[idx++] = e.get<double>();
                }
                if (!allNum) break;
            }
            if (allNum && idx == 16) {
                tr.hasMatrix = true;
                tr.valid = true;
            }
        }
    } else {
        if (t.contains("origin") && isVec3(t["origin"])) tr.origin = toVec3(t["origin"]);
        if (t.contains("xAxis") && isVec3(t["xAxis"])) tr.xAxis = toVec3(t["xAxis"]);
        if (t.contains("yAxis") && isVec3(t["yAxis"])) tr.yAxis = toVec3(t["yAxis"]);
        if (t.contains("zAxis") && isVec3(t["zAxis"])) tr.zAxis = toVec3(t["zAxis"]);
        tr.valid = true;
    }
    return tr;
}

BridgeMesh parseMesh(const json& m) {
    BridgeMesh mesh;
    if (!m.is_object()) return mesh;
    if (m.contains("vertices") && m["vertices"].is_array()) {
        for (const auto& v : m["vertices"]) {
            if (!v.is_array() || v.size() != 3) continue;
            BridgeVec3 p;
            p.x = v[0].is_number() ? v[0].get<double>() : 0.0;
            p.y = v[1].is_number() ? v[1].get<double>() : 0.0;
            p.z = v[2].is_number() ? v[2].get<double>() : 0.0;
            mesh.vertices.push_back(p);
        }
    }
    if (m.contains("triangles") && m["triangles"].is_array()) {
        for (const auto& t : m["triangles"]) {
            if (!t.is_array() || t.size() != 3) continue;
            std::array<int, 3> tri{};
            bool ok = true;
            for (int i = 0; i < 3; ++i) {
                if (!t[i].is_number_integer()) { ok = false; break; }
                tri[i] = static_cast<int>(t[i].get<int64_t>());
                if (tri[i] < 0 || tri[i] >= static_cast<int>(mesh.vertices.size())) { ok = false; break; }
            }
            if (ok) mesh.triangles.push_back(tri);
        }
    }
    return mesh;
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

// geometry dosyasini yukle: bbox + mesh (type=mesh/brep ise).
void loadGeometryFile(const fs::path& root, const std::string& rel,
                      BridgeObject& bo, std::vector<std::string>& errors) {
    if (rel.empty()) return;
    if (!isSafeBridgeRelativePath(rel)) {
        errors.push_back("geometryFile guvenli degil: " + rel);
        return;
    }
    fs::path gp = root / rel;
    json g;
    if (!readJsonFile(gp, g)) {
        errors.push_back("geometry dosyasi okunamadi: " + rel);
        return;
    }
    if (!g.is_object()) return;
    const std::string type = str(g, "type");
    if (g.contains("bbox") && g["bbox"].is_object() &&
        isVec3(g["bbox"].value("min", json())) && isVec3(g["bbox"].value("max", json()))) {
        BridgeBBox bb;
        bb.min = toVec3(g["bbox"]["min"]);
        bb.max = toVec3(g["bbox"]["max"]);
        bo.bbox = bb;
    }
    if ((type == "mesh" || type == "brep") && g.contains("mesh")) {
        bo.mesh = parseMesh(g["mesh"]);
    }
    if (bo.geometryRepresentation.empty()) {
        // geometry dosyasindaki representation alani (varsa) nesneye tasinir.
        bo.geometryRepresentation = str(g, "representation");
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

bool verifyBridgeChecksums(const std::string& dir, std::vector<std::string>& errors) {
    fs::path root(dir);
    fs::path cf = root / "checksums.json";
    std::error_code ec;
    if (!fs::is_regular_file(cf, ec)) return true;  // opsiyonel

    json j;
    if (!readJsonFile(cf, j) || !j.is_object()) {
        errors.push_back("checksums.json okunamadi/nesne degil");
        return false;
    }
    bool ok = true;
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string rel = it.key();
        if (!it.value().is_string()) {
            errors.push_back("checksums[" + rel + "] string degil");
            ok = false;
            continue;
        }
        if (!isSafeBridgeRelativePath(rel)) {
            errors.push_back("checksums anahtari guvenli degil: " + rel);
            ok = false;
            continue;
        }
        fs::path fp = root / rel;
        if (!fs::is_regular_file(fp, ec)) {
            errors.push_back("checksums[" + rel + "] dosya yok");
            ok = false;
            continue;
        }
        std::ifstream in(fp, std::ios::binary);
        std::stringstream buf;
        buf << in.rdbuf();
        const std::string hex = sha256Hex(buf.str());
        std::string want = it.value().get<std::string>();
        for (char& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (hex != want) {
            errors.push_back("checksums[" + rel + "] uyusmuyor (butunluk hatasi)");
            ok = false;
        }
    }
    return ok;
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
    out.manifest.sourceModelName = str(m, "sourceModelName");
    out.manifest.exportTimestamp = str(m, "exportTimestamp");
    out.manifest.coordinateSystem = str(m, "coordinateSystem");
    out.manifest.lengthUnit = str(m, "lengthUnit");
    out.manifest.componentIdentifier = str(m, "componentIdentifier");
    out.manifest.exportedObjectCount = static_cast<int>(integer(m, "exportedObjectCount"));
    out.manifest.knownLimitations = strList(m, "knownLimitations");
    if (m.contains("producer") && m["producer"].is_object()) {
        out.manifest.producerName = str(m["producer"], "name");
        out.manifest.producerVersion = str(m["producer"], "version");
    }

    if (out.manifest.formatVersion != "1.0" && out.manifest.formatVersion != "2.0" &&
        out.manifest.formatVersion != "3.0")
        errors.push_back("manifest.formatVersion 1.0/2.0/3.0 olmali");
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
    const auto parseMember = [](const json& pm) {
        BridgeMemberRef ref;
        ref.guid = str(pm, "guid");
        ref.profile = str(pm, "profile");
        ref.material = str(pm, "material");
        ref.role = str(pm, "role");
        ref.roleKey = str(pm, "roleKey");
        if (pm.contains("startPoint") && isVec3(pm["startPoint"]))
            ref.startPoint = toVec3(pm["startPoint"]);
        if (pm.contains("endPoint") && isVec3(pm["endPoint"]))
            ref.endPoint = toVec3(pm["endPoint"]);
        return ref;
    };
    if (comp.contains("primaryMember") && comp["primaryMember"].is_object())
        out.component.primaryMember = parseMember(comp["primaryMember"]);
    if (comp.contains("secondaryMembers") && comp["secondaryMembers"].is_array())
        for (const auto& sm : comp["secondaryMembers"])
            if (sm.is_object())
                out.component.secondaryMembers.push_back(parseMember(sm));
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
            bo.roleKey = str(o, "roleKey");
            bo.profile = str(o, "profile");
            bo.material = str(o, "material");
            bo.parentSourceGUID = str(o, "parentSourceGUID");
            bo.mapped = boolean(o, "mapped");
            bo.mappedKind = str(o, "mappedKind");
            bo.geometryFile = str(o, "geometryFile");
            bo.geometryRepresentation = str(o, "geometryRepresentation");
            bo.confidence = str(o, "confidence");
            bo.unsupportedReason = str(o, "unsupportedReason");
            if (o.contains("identifierId") && o["identifierId"].is_number_integer())
                bo.identifierId = static_cast<int>(o["identifierId"].get<int64_t>());
            if (bo.sourceGUID.empty()) { errors.push_back("object sourceGUID bos"); continue; }
            if (!seen.insert(bo.sourceGUID).second)
                errors.push_back("object sourceGUID tekrarli: " + bo.sourceGUID);
            // V3: roleKey birincil, role yedek.
            if (bo.roleKey.empty()) bo.roleKey = bo.role;

            loadGeometryFile(root, bo.geometryFile, bo, errors);

            if (o.contains("boltGroup") && o["boltGroup"].is_object())
                bo.boltGroup = parseBoltGroup(o["boltGroup"]);
            if (o.contains("weld") && o["weld"].is_object())
                bo.weld = parseWeld(o["weld"]);
            if (o.contains("topology") && o["topology"].is_object())
                bo.topology = parseTopology(o["topology"]);
            if (o.contains("fitting") && o["fitting"].is_object())
                bo.fitting = parseFitting(o["fitting"]);
            if (o.contains("booleanPart") && o["booleanPart"].is_object())
                bo.booleanPart = parseBooleanPart(o["booleanPart"]);
            if (o.contains("localToGlobal") && o["localToGlobal"].is_object())
                bo.localToGlobal = parseTransform(o["localToGlobal"]);

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
    out.errors = strList(diag, "errors");
    out.unresolvedObjects = strList(diag, "unresolvedObjects");

    // --- V3: relationships.json (opsiyonel) ---
    json rel;
    if (readJsonFile(root / "relationships.json", rel)) {
        if (rel.is_array()) {
            for (const auto& r : rel) {
                if (!r.is_object()) continue;
                BridgeRelationship br;
                br.id = str(r, "id");
                br.kind = str(r, "kind");
                br.sourceGUID = str(r, "sourceGUID");
                br.targetGUID = str(r, "targetGUID");
                br.role = str(r, "role");
                br.confidence = str(r, "confidence");
                if (br.kind.empty() || br.sourceGUID.empty() || br.targetGUID.empty())
                    errors.push_back("relationship eksik alan (kind/sourceGUID/targetGUID)");
                else
                    out.relationships.push_back(std::move(br));
            }
        } else {
            errors.push_back("relationships liste degil");
        }
    }

    // --- V3: operations.json (opsiyonel) ---
    json ops;
    if (readJsonFile(root / "operations.json", ops)) {
        if (ops.is_array()) {
            for (const auto& op : ops) {
                if (!op.is_object()) continue;
                BridgeOperation bo;
                bo.id = str(op, "id");
                bo.type = str(op, "type");
                bo.targetPartGUID = str(op, "targetPartGUID");
                bo.operatorGUID = str(op, "operatorGUID");
                bo.cuttingSolidGUID = str(op, "cuttingSolidGUID");
                if (op.contains("fittingPlane")) bo.fittingPlane = parsePlane(op["fittingPlane"]);
                bo.keepSide = str(op, "keepSide");
                bo.inferred = boolean(op, "inferred");
                bo.confidence = str(op, "confidence");
                bo.finalSolidGeometryFile = str(op, "finalSolidGeometryFile");
                if (bo.id.empty() || bo.type.empty() || bo.targetPartGUID.empty())
                    errors.push_back("operation eksik alan (id/type/targetPartGUID)");
                else
                    out.operations.push_back(std::move(bo));
            }
        } else {
            errors.push_back("operations liste degil");
        }
    }

    // --- V3: checksums.json (opsiyonel) ---
    verifyBridgeChecksums(dir, errors);

    return errors.empty();
}

}  // namespace mm
