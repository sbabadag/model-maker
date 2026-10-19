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
            return true;
        default:
            return false;
    }
}

bool isParametricRelationship(RelationshipKind k) noexcept {
    return k == RelationshipKind::SizeFromMember || k == RelationshipKind::SizeFromParameter;
}

bool isDependencyRelationship(RelationshipKind k) noexcept {
    return k == RelationshipKind::AttachedTo || k == RelationshipKind::DependsOn;
}

bool isOwnershipRelationship(RelationshipKind k) noexcept {
    return k == RelationshipKind::OwnedBy || k == RelationshipKind::Cuts ||
           k == RelationshipKind::Welds;
}

namespace {

// Rezerve roleKey'ler. "component" = component instance'inin kendisi (ownership
// hedefi); "main"/"secondary_N" = kullanici parcalari (sahiplenilmez, referans).
bool isReservedRole(const std::string& key) noexcept {
    if (key == "main" || key == "component") return true;
    if (key.rfind("secondary_", 0) == 0) {
        const std::string suffix = key.substr(std::string("secondary_").size());
        if (suffix.empty()) return false;
        for (char c : suffix)
            if (c < '0' || c > '9') return false;
        return true;
    }
    return false;
}

// Kullanici parcasi (main veya secondary_N). Component bunlari SAHIPLENMEZ.
bool isUserPartRole(const std::string& key) noexcept {
    if (key == "main") return true;
    return key.rfind("secondary_", 0) == 0;
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
    //     nesne/roleKey olmalidir. (Dongu/geometrik celiski burada YOK — cozucu
    //     katmaninda, toConstraintSystem() + ConstraintSystem::solve() ile.)
    for (const auto& r : relationships_) {
        if (!r.enabled) continue;
        const bool toIsParam = (r.kind == RelationshipKind::SizeFromParameter);
        if (!isKnownKey(r.from, objects))
            unresolved.push_back("from: " + r.from);
        if (!toIsParam && !isKnownKey(r.to, objects))
            unresolved.push_back("to: " + r.to);
    }

    // (2) OwnedBy hedefi kullanici parcasi (main/secondary_N) OLAMAZ: component
    //     kullanici parcalarini sahiplenmez, yalniz referanslar (sahiplik hedefi
    //     "component" veya baska bir component-owned nesne olmalidir). Bu bir
    //     YAPISAL sahiplik kuralidir, cozucu degil.
    for (const auto& r : relationships_) {
        if (!r.enabled) continue;
        if (r.kind == RelationshipKind::OwnedBy && isUserPartRole(r.to))
            conflicts.push_back("ownedBy hedefi kullanici parcasi olamaz: " + r.to);
    }

    if (!unresolved.empty()) {
        out.status = RelationshipStatus::Unresolved;
        out.problems.insert(out.problems.end(), unresolved.begin(), unresolved.end());
    }
    if (!conflicts.empty()) {
        out.status = RelationshipStatus::Conflict;
        for (const auto& c : conflicts)
            out.problems.push_back("conflict: " + c);
    }

    out.ok = unresolved.empty() && conflicts.empty();
    return out;
}

std::vector<std::pair<std::string, std::string>> ComponentRelationshipGraph::directedEdges() const {
    std::vector<std::pair<std::string, std::string>> edges;
    for (const auto& r : relationships_) {
        if (!r.enabled) continue;
        if (isDependencyRelationship(r.kind) || isOwnershipRelationship(r.kind))
            edges.emplace_back(r.from, r.to);
    }
    return edges;
}

ConstraintSystem ComponentRelationshipGraph::toConstraintSystem(
    const std::vector<ComponentObjectSpec>& objects) const {
    ConstraintSystem sys;

    // Her nesne roleKey'i bir Point handle'i olur. Rezerve main/secondary_N de
    // handle olarak eklenir (referans butunlugu icin; kullanici parcasi olan
    // main/secondary sahiplenilmez ama cozumde referans noktasi olarak durur).
    auto addHandleForKey = [&](const std::string& key) {
        Vec3 pos{};
        for (const auto& o : objects) {
            if (o.roleKey != key) continue;
            if (o.point) pos = o.point->position;
            else if (o.line) pos = o.line->origin;
            else if (o.plane) pos = o.plane->origin;
            else if (o.plate) pos = o.plate->center;
            break;
        }
        sys.addHandle(HandleKey{key}, HandleKind::Point, pos);
    };

    std::set<std::string> keys;
    for (const auto& o : objects) keys.insert(o.roleKey);
    for (const auto& r : relationships_) {
        if (!r.enabled) continue;
        keys.insert(r.from);
        keys.insert(r.to);
    }
    for (const auto& k : keys) addHandleForKey(k);

    // Dependency yonlu kenarlarini ConstraintSystem bagimliligina aktar.
    // YALNIZCA gercek cozum bagimliligi (AttachedTo/DependsOn) aktarilir;
    // sahiplik kenarlari (OwnedBy/Cuts/Welds) cozum bagimliligi DEGILDIR ve
    // addDependency'e girmez (aksi halde yapay Cyclic uretilebilir).
    for (const auto& r : relationships_) {
        if (!r.enabled) continue;
        if (isDependencyRelationship(r.kind))
            sys.addDependency(HandleKey{r.from}, HandleKey{r.to});
    }

    return sys;
}

// --- GenerationPlan koprusu + roleKey koruyan plan (C1) -----------------------

GenerationPlan generationPlanFromObjects(const std::vector<ComponentObjectSpec>& objects) noexcept {
    GenerationPlan plan;
    for (const auto& o : objects) {
        if (o.plate) plan.plates.push_back(*o.plate);
        else if (o.boltGroup) plan.boltGroups.push_back(*o.boltGroup);
        else if (o.cut) plan.cuts.push_back(*o.cut);
        else if (o.weld) plan.welds.push_back(*o.weld);
        // Construction (point/line/plane) plana dahil edilmez; yalnizca cozumde.
    }
    plan.ok = true;
    return plan;
}

// --- TIP-GUVENLI CHILD TABLOSU (C1) ------------------------------------------

bool ComponentChildTable::insert(const ComponentChildRecord& child) {
    if (!isValidComponentChildRecord(child)) return false;
    const auto it = records_.find(child.roleKey);
    if (it != records_.end()) {
        // Tekrarlanan roleKey: AYNI tür (kind) ise de bile deterministik REDDET
        // (tekrarlanan roleKey yasak). Farkli tur zaten tip-guvenlik ihlali.
        return false;
    }
    records_.emplace(child.roleKey, child);
    return true;
}

bool ComponentChildTable::remove(const std::string& roleKey) {
    return records_.erase(roleKey) > 0;
}

const ComponentChildRecord* ComponentChildTable::find(const std::string& roleKey) const noexcept {
    const auto it = records_.find(roleKey);
    return it == records_.end() ? nullptr : &it->second;
}

EntityId ComponentChildTable::entityIdFor(const std::string& roleKey) const noexcept {
    const ComponentChildRecord* c = find(roleKey);
    return c ? c->entityId : kInvalidEntityId;
}

bool ComponentChildTable::assignEntityId(const std::string& roleKey, EntityId id) {
    auto it = records_.find(roleKey);
    if (it == records_.end()) return false;  // roleKey yok — tablo degismez
    it->second.entityId = id;
    return true;
}

std::vector<ComponentChildRecord> ComponentChildTable::children() const {
    std::vector<ComponentChildRecord> out;
    out.reserve(records_.size());
    for (const auto& [k, v] : records_) out.push_back(v);
    return out;  // std::map -> lexicographic roleKey sirasi (deterministik)
}

// --- ROLEKEY KORUYAN PLAN (C1) ------------------------------------------------

ComponentPlan componentPlanFromObjects(const std::vector<ComponentObjectSpec>& objects,
                                       const ComponentRelationshipGraph& graph) noexcept {
    ComponentPlan out;
    out.plan = generationPlanFromObjects(objects);
    out.relationships = graph;  // C4: TAM grafik korunur (materializer validasyonu)

    // roleKey tekilligi: ayni roleKey iki nesnede olamaz (builder zaten engeller
    // ama burada fail-closed denetlenir). Ayrica kind->payload tutarliligi
    // isValidComponentChildRecord icinde dogrulanir.
    for (const auto& o : objects) {
        ComponentChildRecord child;
        child.roleKey = o.roleKey;
        child.kind = o.kind;
        switch (o.kind) {
            case ComponentObjectKind::Plate:
                if (o.plate) child.plate = *o.plate;
                break;
            case ComponentObjectKind::BoltGroup:
                if (o.boltGroup) child.boltGroup = *o.boltGroup;
                break;
            case ComponentObjectKind::Cut:
                if (o.cut) child.cut = *o.cut;
                break;
            case ComponentObjectKind::Weld:
                if (o.weld) child.weld = *o.weld;
                break;
            default:
                break;  // construction — plana dahil edilmez
        }
        if (child.kind == ComponentObjectKind::ConstructionPoint ||
            child.kind == ComponentObjectKind::ConstructionLine ||
            child.kind == ComponentObjectKind::ConstructionPlane) {
            continue;  // EntityId uretmez; yalnizca cozum asamasinda
        }
        if (!isValidComponentChildRecord(child)) {
            out.ok = false;
            out.error = L"gecersiz child (kind-payload uyusmazligi): " +
                        std::wstring(child.roleKey.begin(), child.roleKey.end());
            return out;
        }
        for (const auto& existing : out.children) {
            if (existing.roleKey == child.roleKey) {
                out.ok = false;
                out.error = L"tekrarlanan roleKey: " +
                            std::wstring(child.roleKey.begin(), child.roleKey.end());
                return out;
            }
        }
        out.children.push_back(std::move(child));
    }

    // dependencies: AttachedTo/DependsOn (from -> to) kenarlarindan. Bir child
    // yalnizca baska bir CHILD'e veya rezerve role'e bagli olabilir; burada
    // yalnizca roleKey listesi toplanir (cozum/kisit uretimi ConstraintSystem'da).
    for (const auto& r : graph.relationships()) {
        if (!r.enabled) continue;
        if (!isDependencyRelationship(r.kind)) continue;
        for (auto& child : out.children) {
            if (child.roleKey == r.from) {
                if (std::find(child.dependencies.begin(), child.dependencies.end(), r.to) ==
                    child.dependencies.end()) {
                    child.dependencies.push_back(r.to);
                }
            }
        }
    }

    // Deterministik sira: lexicographic roleKey (ComponentChildTable ile ayni).
    std::sort(out.children.begin(), out.children.end(),
              [](const ComponentChildRecord& a, const ComponentChildRecord& b) {
                  return a.roleKey < b.roleKey;
              });

    out.ok = true;
    return out;
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
