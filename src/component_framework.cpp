#include "model_maker/component_framework.hpp"

#include <algorithm>
#include <functional>
#include <set>
#include <unordered_map>

namespace mm {

// --- roleKey dogrulama -------------------------------------------------------

bool isValidRoleKey(const std::string& roleKey) noexcept {
    if (roleKey.empty()) return false;
    for (char c : roleKey) {
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = (c >= '0' && c <= '9');
        if (!alpha && !digit && c != '_') return false;
    }
    return true;
}

// --- iliski aileleri ---------------------------------------------------------

bool isGeometricRelationship(RelationshipKind k) noexcept {
    switch (k) {
        case RelationshipKind::Coincident:
        case RelationshipKind::Offset:
        case RelationshipKind::Parallel:
        case RelationshipKind::Perpendicular:
        case RelationshipKind::FaceContact:
        case RelationshipKind::Align:
        case RelationshipKind::FollowFrame:
        case RelationshipKind::SizeFromMember:
        case RelationshipKind::SizeFromParameter:
            return true;
        default:
            return false;
    }
}

bool isDependencyRelationship(RelationshipKind k) noexcept {
    return k == RelationshipKind::AttachedTo || k == RelationshipKind::DependsOn;
}

bool isOwnershipRelationship(RelationshipKind k) noexcept {
    return k == RelationshipKind::OwnedBy || k == RelationshipKind::Cuts ||
           k == RelationshipKind::Welds;
}

namespace {

// Rezerve roleKey'ler (kullanici parcalari). "secondary_N" N>=0.
bool isReservedRole(const std::string& key) noexcept {
    if (key == "main") return true;
    if (key.rfind("secondary_", 0) == 0) {
        const std::string suffix = key.substr(std::string("secondary_").size());
        if (suffix.empty()) return false;
        for (char c : suffix)
            if (c < '0' || c > '9') return false;
        return true;
    }
    return false;
}

// Gecerli roleKey: nesne roleKey'i, rezerve main/secondary, veya parametre adi
// (SizeFromParameter hedefi object degil parametre olabilir).
bool isKnownKey(const std::string& key,
                const std::vector<ComponentObjectSpec>& objects) noexcept {
    if (isReservedRole(key)) return true;
    for (const auto& o : objects)
        if (o.roleKey == key) return true;
    return false;
}

} // namespace

// --- ComponentRelationshipGraph ----------------------------------------------

void ComponentRelationshipGraph::add(const Relationship& r) {
    relationships_.push_back(r);
}

ComponentRelationshipGraph::Validation ComponentRelationshipGraph::validate(
    const std::vector<ComponentObjectSpec>& objects) const {
    Validation out;
    std::vector<std::string> unresolved;
    std::vector<std::string> conflicts;

    // (1) Unresolved: from/to bilinen bir roleKey degil. SizeFromParameter'da
    //     'to' bir PARAMETRE adidir (nesne degil) -> muaf. 'from' her zaman
    //     nesne/roleKey olmalidir.
    for (const auto& r : relationships_) {
        if (!r.enabled) continue;
        const bool toIsParam = (r.kind == RelationshipKind::SizeFromParameter);
        if (!isKnownKey(r.from, objects))
            unresolved.push_back("from: " + r.from);
        if (!toIsParam && !isKnownKey(r.to, objects))
            unresolved.push_back("to: " + r.to);
    }

    // (2) Conflict: ayni ikili uzerinde hem Parallel hem Perpendicular.
    std::set<std::pair<std::string, std::string>> seenPairs;
    for (const auto& r : relationships_) {
        if (!r.enabled) continue;
        if (r.kind != RelationshipKind::Parallel &&
            r.kind != RelationshipKind::Perpendicular)
            continue;
        const std::string a = std::min(r.from, r.to);
        const std::string b = std::max(r.from, r.to);
        seenPairs.insert({a, b});
    }
    // Parallel + Perpendicular ayni ikili icin tekrar tarama ile celiski bul.
    std::unordered_map<std::string, std::set<RelationshipKind>> pairKinds;
    for (const auto& r : relationships_) {
        if (!r.enabled) continue;
        if (r.kind != RelationshipKind::Parallel &&
            r.kind != RelationshipKind::Perpendicular)
            continue;
        const std::string a = std::min(r.from, r.to);
        const std::string b = std::max(r.from, r.to);
        pairKinds[a + "|" + b].insert(r.kind);
    }
    for (const auto& [key, kinds] : pairKinds) {
        if (kinds.size() > 1) {
            const auto sep = key.find('|');
            conflicts.push_back(key.substr(0, sep) + " <-> " + key.substr(sep + 1));
        }
    }

    // (3) Cyclic: yonlu dependency/ownership kenarlarinda DFS.
    const std::vector<std::string> cycle = findCycles();

    if (!unresolved.empty()) {
        out.status = RelationshipStatus::Unresolved;
        out.problems.insert(out.problems.end(), unresolved.begin(), unresolved.end());
    }
    if (!conflicts.empty()) {
        out.status = RelationshipStatus::Conflict;
        for (const auto& c : conflicts)
            out.problems.push_back("conflict: " + c);
    }
    if (!cycle.empty()) {
        out.status = RelationshipStatus::Cyclic;
        out.problems.push_back("cycle: " +
                               [&] {
                                   std::string s;
                                   for (std::size_t i = 0; i < cycle.size(); ++i) {
                                       if (i) s += " -> ";
                                       s += cycle[i];
                                   }
                                   return s;
                               }());
    }

    out.ok = unresolved.empty() && conflicts.empty() && cycle.empty();
    return out;
}

std::vector<std::string> ComponentRelationshipGraph::findCycles() const {
    // Yonlu kenarlar: AttachedTo (from -> to), DependsOn (from -> to),
    // OwnedBy (from -> to), Cuts (cutter -> target), Welds (weld -> part).
    std::unordered_map<std::string, std::vector<std::string>> adj;
    for (const auto& r : relationships_) {
        if (!r.enabled) continue;
        if (isDependencyRelationship(r.kind) || isOwnershipRelationship(r.kind)) {
            // Yon: 'from' -> 'to' (OwnedBy: child -> owner, yani bagimlilik yonu
            // child -> owner; dongu tespiti icin bu yeterli).
            adj[r.from].push_back(r.to);
        }
    }

    // Beyaz/gri/siyah DFS ile dongu tespiti.
    enum Color { White, Gray, Black };
    std::unordered_map<std::string, Color> color;
    std::vector<std::string> allKeys;
    for (const auto& [k, _] : adj) {
        (void)_;
        color[k] = White;
        allKeys.push_back(k);
    }
    for (const auto& [k, v] : adj) {
        (void)v;
        // to'lar da anahtar listesine girsin (yaprak dongusuz dahi).
        if (color.find(k) == color.end()) {
            color[k] = White;
            allKeys.push_back(k);
        }
    }
    for (const auto& [k, v] : adj)
        for (const auto& t : v)
            if (color.find(t) == color.end()) {
                color[t] = White;
                allKeys.push_back(t);
            }

    std::sort(allKeys.begin(), allKeys.end());

    std::vector<std::string> cycleStack;
    std::vector<std::string> cycleFound;

    std::function<bool(const std::string&)> dfs = [&](const std::string& node) {
        color[node] = Gray;
        cycleStack.push_back(node);
        auto it = adj.find(node);
        if (it != adj.end()) {
            for (const auto& next : it->second) {
                if (color[next] == Gray) {
                    // Dongu bulundu: cycleStack'te next'ten itibaren.
                    auto pos = std::find(cycleStack.begin(), cycleStack.end(), next);
                    if (pos != cycleStack.end()) {
                        cycleFound.assign(pos, cycleStack.end());
                        cycleFound.push_back(next);
                        return true;
                    }
                } else if (color[next] == White) {
                    if (dfs(next)) return true;
                }
            }
        }
        cycleStack.pop_back();
        color[node] = Black;
        return false;
    };

    for (const auto& node : allKeys) {
        if (color[node] == White) {
            if (dfs(node)) break;
        }
    }
    return cycleFound;
}

// --- ComponentBuilder --------------------------------------------------------

namespace {

template <typename PayloadSetter>
bool addObject(std::vector<ComponentObjectSpec>& objects, const std::string& roleKey,
               ComponentObjectKind kind, PayloadSetter setter, std::wstring& error) {
    if (!isValidRoleKey(roleKey)) {
        error = L"gecersiz roleKey: " +
                std::wstring(roleKey.begin(), roleKey.end());
        return false;
    }
    for (const auto& o : objects) {
        if (o.roleKey == roleKey) {
            error = L"tekrarlanan roleKey: " +
                    std::wstring(roleKey.begin(), roleKey.end());
            return false;
        }
    }
    ComponentObjectSpec spec;
    spec.roleKey = roleKey;
    spec.kind = kind;
    setter(spec);
    objects.push_back(std::move(spec));
    return true;
}

} // namespace

ComponentBuilder& ComponentBuilder::plate(const std::string& roleKey, const PlateSpec& s) {
    addObject(objects_, roleKey, ComponentObjectKind::Plate,
              [&](ComponentObjectSpec& o) { o.plate = s; }, error_);
    return *this;
}
ComponentBuilder& ComponentBuilder::boltGroup(const std::string& roleKey, const BoltGroup& s) {
    addObject(objects_, roleKey, ComponentObjectKind::BoltGroup,
              [&](ComponentObjectSpec& o) { o.boltGroup = s; }, error_);
    return *this;
}
ComponentBuilder& ComponentBuilder::cut(const std::string& roleKey, const CutModifier& s) {
    addObject(objects_, roleKey, ComponentObjectKind::Cut,
              [&](ComponentObjectSpec& o) { o.cut = s; }, error_);
    return *this;
}
ComponentBuilder& ComponentBuilder::weld(const std::string& roleKey, const WeldSpec& s) {
    addObject(objects_, roleKey, ComponentObjectKind::Weld,
              [&](ComponentObjectSpec& o) { o.weld = s; }, error_);
    return *this;
}
ComponentBuilder& ComponentBuilder::point(const std::string& roleKey, const Vec3& pos) {
    addObject(objects_, roleKey, ComponentObjectKind::ConstructionPoint,
              [&](ComponentObjectSpec& o) { o.point = ConstructionPointSpec{pos}; }, error_);
    return *this;
}
ComponentBuilder& ComponentBuilder::line(const std::string& roleKey, const Vec3& origin,
                                         const Vec3& dir) {
    addObject(objects_, roleKey, ComponentObjectKind::ConstructionLine,
              [&](ComponentObjectSpec& o) { o.line = ConstructionLineSpec{origin, dir}; }, error_);
    return *this;
}
ComponentBuilder& ComponentBuilder::plane(const std::string& roleKey, const Vec3& origin,
                                          const Vec3& normal) {
    addObject(objects_, roleKey, ComponentObjectKind::ConstructionPlane,
              [&](ComponentObjectSpec& o) { o.plane = ConstructionPlaneSpec{origin, normal}; }, error_);
    return *this;
}

ComponentBuilder& ComponentBuilder::coincident(const std::string& a, const std::string& b) {
    graph_.add({a, b, RelationshipKind::Coincident, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::offset(const std::string& a, const std::string& b,
                                           double distance) {
    graph_.add({a, b, RelationshipKind::Offset, {}, distance, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::parallel(const std::string& a, const std::string& b) {
    graph_.add({a, b, RelationshipKind::Parallel, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::perpendicular(const std::string& a, const std::string& b) {
    graph_.add({a, b, RelationshipKind::Perpendicular, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::faceContact(const std::string& a, const std::string& b) {
    graph_.add({a, b, RelationshipKind::FaceContact, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::align(const std::string& a, const std::string& b,
                                          const Vec3& offset_) {
    graph_.add({a, b, RelationshipKind::Align, offset_, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::followFrame(const std::string& a, const std::string& b) {
    graph_.add({a, b, RelationshipKind::FollowFrame, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::sizeFromMember(const std::string& obj,
                                                   const std::string& member) {
    graph_.add({obj, member, RelationshipKind::SizeFromMember, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::sizeFromParameter(const std::string& obj,
                                                      const std::string& param) {
    graph_.add({obj, param, RelationshipKind::SizeFromParameter, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::attachedTo(const std::string& a, const std::string& b) {
    graph_.add({a, b, RelationshipKind::AttachedTo, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::cuts(const std::string& cutter, const std::string& target) {
    graph_.add({cutter, target, RelationshipKind::Cuts, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::welds(const std::string& weld_, const std::string& partA,
                                          const std::string& partB) {
    graph_.add({weld_, partA, RelationshipKind::Welds, {}, 0.0, true});
    graph_.add({weld_, partB, RelationshipKind::Welds, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::ownedBy(const std::string& child, const std::string& owner) {
    graph_.add({child, owner, RelationshipKind::OwnedBy, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::dependsOn(const std::string& a, const std::string& b) {
    graph_.add({a, b, RelationshipKind::DependsOn, {}, 0.0, true});
    return *this;
}
ComponentBuilder& ComponentBuilder::relationship(const Relationship& r) {
    graph_.add(r);
    return *this;
}

ComponentBuilder::Result ComponentBuilder::build() const {
    Result out;
    out.objects = objects_;
    out.relationships = graph_;

    // addObject hatalari (gecersiz/tekrarlanan roleKey) -> fail-closed.
    if (!error_.empty()) {
        out.ok = false;
        out.error = error_;
        return out;
    }

    const auto validation = graph_.validate(objects_);
    if (!validation.ok) {
        out.ok = false;
        for (const auto& p : validation.problems)
            out.error += std::wstring(p.begin(), p.end()) + L"; ";
        return out;
    }
    out.ok = true;
    return out;
}

} // namespace mm
