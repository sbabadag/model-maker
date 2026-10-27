#include "model_maker/marking.hpp"

#include "model_maker/utf8.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace mm {
namespace {

using json = nlohmann::json;
using text::fromUtf8;
using text::toUtf8;

constexpr int kSchemaVersion = 1;

std::uint64_t fnvInit() { return 1469598103934665603ULL; }
void fnvMix(std::uint64_t& h, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (i * 8)) & 0xFFu;
        h *= 1099511628211ULL;
    }
}
std::int64_t quant(double v) { return static_cast<std::int64_t>(std::llround(v * 10.0)); }
double dist(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

bool PartSignature::operator<(const PartSignature& o) const {
    return std::tie(profile, material, name, finish, length, shapeHash, faceCount, vertexCount) <
           std::tie(o.profile, o.material, o.name, o.finish, o.length, o.shapeHash, o.faceCount,
                    o.vertexCount);
}

bool AssemblySignature::operator<(const AssemblySignature& o) const {
    return std::tie(mainPartMark, memberPartMarks, layoutHash) <
           std::tie(o.mainPartMark, o.memberPartMarks, o.layoutHash);
}

PartSignature partSignatureOf(const WireframeModel& model) {
    const EntityProperties& pr = model.properties();
    PartSignature sig;
    sig.profile = fromUtf8(pr.profileName);
    sig.material = fromUtf8(pr.material);
    sig.name = fromUtf8(pr.name);
    sig.finish = fromUtf8(pr.finish);
    sig.faceCount = static_cast<int>(model.faces().size());
    sig.vertexCount = static_cast<int>(model.vertices().size());

    const auto& v = model.vertices();
    std::vector<std::int64_t> lens;
    lens.reserve(model.edges().size());
    double longest = 0.0;
    for (const Edge& e : model.edges()) {
        if (e.from >= v.size() || e.to >= v.size()) continue;
        const double l = dist(v[e.from], v[e.to]);
        longest = std::max(longest, l);
        lens.push_back(quant(l));
    }
    std::sort(lens.begin(), lens.end());
    std::uint64_t h = fnvInit();
    for (const std::int64_t q : lens) fnvMix(h, static_cast<std::uint64_t>(q));
    sig.shapeHash = h;

    const Vec3 a{pr.axisFromX, pr.axisFromY, pr.axisFromZ};
    const Vec3 b{pr.axisToX, pr.axisToY, pr.axisToZ};
    const double axisLen = dist(a, b);
    sig.length = quant(axisLen > 1e-6 ? axisLen : longest);
    return sig;
}

std::uint64_t layoutHashOf(const std::vector<Vec3>& c) {
    std::vector<std::int64_t> d;
    for (std::size_t i = 0; i < c.size(); ++i)
        for (std::size_t j = i + 1; j < c.size(); ++j) d.push_back(quant(dist(c[i], c[j])));
    std::sort(d.begin(), d.end());
    std::uint64_t h = fnvInit();
    fnvMix(h, c.size());
    for (const std::int64_t q : d) fnvMix(h, static_cast<std::uint64_t>(q));
    return h;
}

MarkRegistry::MarkRegistry(MarkFormat format)
    : format_(std::move(format)),
      nextPart_(std::max(1, format_.partStart)),
      nextAssembly_(std::max(1, format_.assemblyStart)) {}

MarkResult MarkRegistry::markPart(EntityId part, const PartSignature& signature) {
    MarkResult r;
    if (part == kInvalidEntityId) return r;
    int number;
    const auto it = partByKey_.find(signature);
    if (it != partByKey_.end()) {
        number = it->second;
    } else {
        number = nextPart_++;
        partSigs_[number] = signature;
        partByKey_[signature] = number;
        r.isNew = true;
    }
    const Key key{MarkKind::Part, number};
    const auto as = assignments_.find(part);
    r.changed = as != assignments_.end() && !(as->second.kind == key.kind && as->second.number == key.number);
    assignments_[part] = key;
    r.mark = Mark{MarkKind::Part, number, format_.partPrefix};
    return r;
}

MarkResult MarkRegistry::markAssembly(EntityId assembly, const AssemblySignature& signatureIn) {
    MarkResult r;
    if (assembly == kInvalidEntityId) return r;
    AssemblySignature signature = signatureIn;
    std::sort(signature.memberPartMarks.begin(), signature.memberPartMarks.end());
    int number;
    const auto it = assemblyByKey_.find(signature);
    if (it != assemblyByKey_.end()) {
        number = it->second;
    } else {
        number = nextAssembly_++;
        assemblySigs_[number] = signature;
        assemblyByKey_[signature] = number;
        r.isNew = true;
    }
    const Key key{MarkKind::Assembly, number};
    const auto as = assignments_.find(assembly);
    r.changed = as != assignments_.end() && !(as->second.kind == key.kind && as->second.number == key.number);
    assignments_[assembly] = key;
    r.mark = Mark{MarkKind::Assembly, number, format_.assemblyPrefix};
    return r;
}

std::optional<Mark> MarkRegistry::markOf(EntityId id) const {
    const auto it = assignments_.find(id);
    if (it == assignments_.end()) return std::nullopt;
    return Mark{it->second.kind, it->second.number,
                it->second.kind == MarkKind::Part ? format_.partPrefix : format_.assemblyPrefix};
}

std::vector<EntityId> MarkRegistry::membersOf(const Mark& mark) const {
    std::vector<EntityId> out;
    for (const auto& [id, key] : assignments_)
        if (key.kind == mark.kind && key.number == mark.number) out.push_back(id);
    return out; // std::map -> artan EntityId
}

const PartSignature* MarkRegistry::partSignatureFor(const Mark& mark) const {
    if (mark.kind != MarkKind::Part) return nullptr;
    const auto it = partSigs_.find(mark.number);
    return it == partSigs_.end() ? nullptr : &it->second;
}

const AssemblySignature* MarkRegistry::assemblySignatureFor(const Mark& mark) const {
    if (mark.kind != MarkKind::Assembly) return nullptr;
    const auto it = assemblySigs_.find(mark.number);
    return it == assemblySigs_.end() ? nullptr : &it->second;
}

bool MarkRegistry::forget(EntityId id) { return assignments_.erase(id) > 0; }

std::string MarkRegistry::serialize() const {
    json root;
    root["version"] = kSchemaVersion;
    root["format"] = {{"partPrefix", toUtf8(format_.partPrefix)},
                      {"assemblyPrefix", toUtf8(format_.assemblyPrefix)},
                      {"partStart", format_.partStart},
                      {"assemblyStart", format_.assemblyStart}};
    root["nextPart"] = nextPart_;
    root["nextAssembly"] = nextAssembly_;
    json parts = json::array();
    for (const auto& [n, s] : partSigs_)
        parts.push_back({{"number", n}, {"profile", toUtf8(s.profile)},
                         {"material", toUtf8(s.material)}, {"name", toUtf8(s.name)},
                         {"finish", toUtf8(s.finish)}, {"length", s.length},
                         {"shapeHash", s.shapeHash}, {"faceCount", s.faceCount},
                         {"vertexCount", s.vertexCount}});
    root["parts"] = parts;
    json asms = json::array();
    for (const auto& [n, s] : assemblySigs_)
        asms.push_back({{"number", n}, {"mainPartMark", s.mainPartMark},
                        {"memberPartMarks", s.memberPartMarks}, {"layoutHash", s.layoutHash}});
    root["assemblies"] = asms;
    json as = json::array();
    for (const auto& [id, k] : assignments_)
        as.push_back({{"id", id}, {"kind", k.kind == MarkKind::Part ? "part" : "assembly"},
                      {"number", k.number}});
    root["assignments"] = as;
    return root.dump(2);
}

bool MarkRegistry::deserialize(const std::string& text, MarkRegistry& out, std::string& error) {
    error.clear();
    try {
        const json root = json::parse(text);
        if (!root.is_object() || root.value("version", 0) != kSchemaVersion) {
            error = "mark kaydi: surum tanimsiz/desteklenmiyor";
            return false;
        }
        MarkFormat f;
        const json& jf = root.at("format");
        f.partPrefix = fromUtf8(jf.at("partPrefix").get<std::string>());
        f.assemblyPrefix = fromUtf8(jf.at("assemblyPrefix").get<std::string>());
        f.partStart = jf.at("partStart").get<int>();
        f.assemblyStart = jf.at("assemblyStart").get<int>();
        MarkRegistry r(f);
        r.nextPart_ = root.at("nextPart").get<int>();
        r.nextAssembly_ = root.at("nextAssembly").get<int>();
        for (const json& j : root.at("parts")) {
            PartSignature s;
            const int n = j.at("number").get<int>();
            s.profile = fromUtf8(j.at("profile").get<std::string>());
            s.material = fromUtf8(j.at("material").get<std::string>());
            s.name = fromUtf8(j.at("name").get<std::string>());
            s.finish = fromUtf8(j.at("finish").get<std::string>());
            s.length = j.at("length").get<std::int64_t>();
            s.shapeHash = j.at("shapeHash").get<std::uint64_t>();
            s.faceCount = j.at("faceCount").get<int>();
            s.vertexCount = j.at("vertexCount").get<int>();
            if (n < 1 || r.partSigs_.count(n) || r.partByKey_.count(s) || n >= r.nextPart_) {
                error = "mark kaydi: gecersiz/yinelenen part numarasi";
                return false;
            }
            r.partSigs_[n] = s;
            r.partByKey_[s] = n;
        }
        for (const json& j : root.at("assemblies")) {
            AssemblySignature s;
            const int n = j.at("number").get<int>();
            s.mainPartMark = j.at("mainPartMark").get<int>();
            s.memberPartMarks = j.at("memberPartMarks").get<std::vector<int>>();
            s.layoutHash = j.at("layoutHash").get<std::uint64_t>();
            if (n < 1 || r.assemblySigs_.count(n) || r.assemblyByKey_.count(s) ||
                n >= r.nextAssembly_) {
                error = "mark kaydi: gecersiz/yinelenen assembly numarasi";
                return false;
            }
            r.assemblySigs_[n] = s;
            r.assemblyByKey_[s] = n;
        }
        for (const json& j : root.at("assignments")) {
            const EntityId id = j.at("id").get<EntityId>();
            const std::string kind = j.at("kind").get<std::string>();
            const int n = j.at("number").get<int>();
            const bool isPart = kind == "part";
            if (id == kInvalidEntityId || (!isPart && kind != "assembly") ||
                !(isPart ? r.partSigs_.count(n) : r.assemblySigs_.count(n))) {
                error = "mark kaydi: atama gecersiz bir mark'a isaret ediyor";
                return false;
            }
            r.assignments_[id] = Key{isPart ? MarkKind::Part : MarkKind::Assembly, n};
        }
        out = std::move(r);
        return true;
    } catch (const std::exception& e) {
        error = std::string("mark kaydi okunamadi: ") + e.what();
        return false;
    }
}

} // namespace mm
