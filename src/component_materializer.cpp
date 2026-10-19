#ifdef MM_HAS_OCC
#include "model_maker/component_materializer.hpp"

#include "model_maker/bolt.hpp"
#include "model_maker/bolt_geometry.hpp"
#include "model_maker/cut_modifier.hpp"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace mm {

namespace {

bool axisDir(const Vec3& a, gp_Dir& out) noexcept {
    const double len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    if (len < 1e-12) return false;
    out = gp_Dir(a.x / len, a.y / len, a.z / len);
    return true;
}

}  // namespace

ComponentMaterializer::ComponentMaterializer(Document& doc)
    : doc_(doc), store_(asSolidStore(&doc.occStore())) {}

TopoDS_Shape ComponentMaterializer::buildPlateSolid(const PlateSpec& p) {
    if (p.width <= 0.0 || p.height <= 0.0 || p.thickness <= 0.0) return TopoDS_Shape();
    gp_Dir z, uu, vv;
    if (!axisDir(p.axis, z) || !axisDir(p.u, uu) || !axisDir(p.v, vv)) return TopoDS_Shape();
    const double hw = p.width * 0.5;
    const double hh = p.height * 0.5;
    const gp_Pnt c(p.center.x, p.center.y, p.center.z);
    const gp_Pnt base = c.Translated(gp_Vec(z) * (-p.thickness * 0.5));
    BRepBuilderAPI_MakePolygon poly;
    const auto corner = [&](double su, double sv) {
        return base.Translated(gp_Vec(uu) * (su * hw) + gp_Vec(vv) * (sv * hh));
    };
    poly.Add(corner(-1.0, -1.0));
    poly.Add(corner(1.0, -1.0));
    poly.Add(corner(1.0, 1.0));
    poly.Add(corner(-1.0, 1.0));
    poly.Close();
    BRepBuilderAPI_MakeFace face(poly.Wire());
    if (!face.IsDone()) return TopoDS_Shape();
    return BRepPrimAPI_MakePrism(face.Face(), gp_Vec(z) * p.thickness).Shape();
}

ComponentMaterializer::Prepared ComponentMaterializer::prepare(
    const ComponentPlan& plan, EntityId mainId,
    const std::vector<EntityId>& secondaryIds,
    const GeometryReferenceResolver* resolver) const {
    Prepared out;
    if (!plan.ok) {
        out.error = plan.error;
        return out;
    }

    // roleKey -> child indeksi (bagimlilik cozumu icin).
    std::unordered_map<std::string, std::size_t> roleIndex;
    for (std::size_t i = 0; i < plan.children.size(); ++i)
        roleIndex[plan.children[i].roleKey] = i;

    for (const auto& child : plan.children) {
        PreparedChild pc;
        pc.record = child;  // entityId henuz kInvalid (commit'te atanir)
        pc.record.entityId = kInvalidEntityId;

        switch (child.kind) {
            case ComponentObjectKind::Plate: {
                if (!child.plate) {
                    out.error = L"Plate child plate payload eksik: " +
                                std::wstring(child.roleKey.begin(), child.roleKey.end());
                    return out;
                }
                pc.solid = buildPlateSolid(*child.plate);
                if (pc.solid.IsNull()) {
                    out.error = L"levha kati uretilemedi: " +
                                std::wstring(child.roleKey.begin(), child.roleKey.end());
                    return out;
                }
                if (shapeVolume(pc.solid) <= 0.0) {
                    out.error = L"levha kati hacimsiz: " +
                                std::wstring(child.roleKey.begin(), child.roleKey.end());
                    return out;
                }
                break;
            }
            case ComponentObjectKind::BoltGroup: {
                if (!child.boltGroup) {
                    out.error = L"BoltGroup child payload eksik: " +
                                std::wstring(child.roleKey.begin(), child.roleKey.end());
                    return out;
                }
                if (!isBoltAxisValid(child.boltGroup->axis)) {
                    out.error = L"bulon ekseni gecersiz: " +
                                std::wstring(child.roleKey.begin(), child.roleKey.end());
                    return out;
                }
                // Montaj katisi (display): grip uzunlugu commit sonrasi Document
                // uzerinden net hesaplanamadigindan, hazirlikta nominal grip
                // (levha kalinligi + 15 mm fallback) kullanilir. Gercek delikler
                // boltGroupHoleModifiers ile (holeDepth dikkate alinir) uretilir.
                const double grip =
                    (child.boltGroup->holeDepth > 0.0)
                        ? child.boltGroup->holeDepth
                        : 15.0;  // levha kalinligi commit'te cozumlenince guncellenir
                pc.boltGroup = *child.boltGroup;
                pc.solid = buildBoltGroupAssemblyShape(pc.boltGroup, grip);
                if (pc.solid.IsNull()) {
                    out.error = L"bulon montaji uretilemedi: " +
                                std::wstring(child.roleKey.begin(), child.roleKey.end());
                    return out;
                }
                break;
            }
            case ComponentObjectKind::Cut: {
                if (!child.cut) {
                    out.error = L"Cut child payload eksik: " +
                                std::wstring(child.roleKey.begin(), child.roleKey.end());
                    return out;
                }
                pc.cut = *child.cut;
                // HEDEF COZUMU (A1.1): cut.target zaten gecerli EntityId ise
                // (generator yolu) korunur. Degilse, Cuts iliskisinin hedef
                // roleKey'i kaydedilir ve commit'te EntityId'ye cozulur.
                if (pc.cut.target == kInvalidEntityId) {
                    const std::string& key = child.roleKey;
                    for (const auto& r : plan.relationships.relationships()) {
                        if (!r.enabled) continue;
                        if (r.kind == RelationshipKind::Cuts && r.from == key) {
                            pc.cutTargetRole = r.to;
                            break;
                        }
                    }
                    if (pc.cutTargetRole.empty()) {
                        out.error = L"Cut hedefi cozulemedi (target EntityId ve Cuts roleKey yok): " +
                                    std::wstring(child.roleKey.begin(), child.roleKey.end());
                        return out;
                    }
                    // Hedef roleKey gecerli olmali: main / secondary_N / component child.
                    const bool isUser = pc.cutTargetRole == "main" ||
                                        pc.cutTargetRole.rfind("secondary_", 0) == 0;
                    if (!isUser && roleIndex.find(pc.cutTargetRole) == roleIndex.end()) {
                        out.error = L"Cut hedefi cozulemedi (bilinmeyen roleKey): '" +
                                    std::wstring(pc.cutTargetRole.begin(), pc.cutTargetRole.end()) + L"'";
                        return out;
                    }
                }
                break;
            }
            case ComponentObjectKind::Weld:
                // GERCEK weld geometri motoru yok — acik Unsupported (sessiz
                // "basari" URETILMEZ; C2 kurali).
                pc.weldUnsupported = true;
                out.error = L"Weld desteklenmiyor (weld geometri motoru yok): " +
                            std::wstring(child.roleKey.begin(), child.roleKey.end());
                return out;
            default:
                // Construction (point/line/plane) plana dahil edilmemeliydi.
                out.error = L"desteklenmeyen child kind: " +
                            std::wstring(child.roleKey.begin(), child.roleKey.end());
                return out;
        }
        out.children.push_back(std::move(pc));
    }

    // Bagimlilik cozumu: her child icin dependencies roleKey'leri cozulmus mu?
    // (unresolved -> Unresolved hatasi; cozum ConstraintSystem'a delege — C4.)
    for (const auto& pc : out.children) {
        for (const auto& dep : pc.record.dependencies) {
            if (dep == "main" || dep == "component") continue;
            if (dep.rfind("secondary_", 0) == 0) continue;
            if (roleIndex.find(dep) == roleIndex.end()) {
                out.error = L"cozulemeyen bagimlilik '" +
                            std::wstring(dep.begin(), dep.end()) + L"' (child '" +
                            std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end()) + L"'";
                return out;
            }
        }
    }

    // A2: iliski dogrulugu MEVCUT ConstraintSystem + GeometryReferenceResolver ile
    // (ikinci solver YOK). Tek ConstraintSystem kurulur; hem bagimlilik dongusu
    // hem geometrik mesafe kisitlari ayni motorla cozulur.
    {
        ConstraintSystem sys;
        for (const auto& pc : out.children) {
            Vec3 pos{};
            if (pc.record.plate) pos = pc.record.plate->center;
            else if (pc.record.boltGroup) pos = pc.record.boltGroup->origin;
            else if (pc.record.cut) pos = pc.record.cut->planeOrigin;
            sys.addHandle(HandleKey{pc.record.roleKey}, HandleKind::Point, pos);
        }
        // (a) Bagimlilik: AttachedTo/DependsOn -> addDependency (ownership YOK).
        for (const auto& pc : out.children) {
            for (const auto& dep : pc.record.dependencies) {
                if (dep == "main" || dep == "component" || dep.rfind("secondary_", 0) == 0)
                    continue;  // rezerve/user part — cozum bagimliligi degil
                if (roleIndex.find(dep) != roleIndex.end())
                    sys.addDependency(HandleKey{pc.record.roleKey}, HandleKey{dep});
            }
        }
        // (b) Geometric mesafe iliskileri -> DistanceConstraint (A2.2). Hedef roleKey
        //     "main"/"secondary_N" (kullanici parcasi) resolver ile ReferencePlane'e
        //     cozulur; cozulemezse Unresolved (SESSIZ basari yok).
        // (c) Orientation (Parallel/Perpendicular/FollowFrame) -> acik Unsupported
        //     (OrientationConstraint dilimi YOK; sahte cozum uretilmez — A2.5).
        // A2 (rev): her mesafe kisiti URETILEN geometrinin GERCEK konumuyla
        // dogrulanir — cozumcu konumu yalniz durum kontrolu degil, child'in
        // hazirlanmis konumunun denklemi sagladigi da test edilir (sessiz
        // uyumsuzluk yok).
        struct DistCheck { std::string roleKey; Vec3 normal; double rhs; };
        std::vector<DistCheck> distChecks;
        for (const auto& r : plan.relationships.relationships()) {
            if (!r.enabled) continue;
            switch (r.kind) {
                case RelationshipKind::Coincident:
                case RelationshipKind::Offset:
                case RelationshipKind::Align:
                case RelationshipKind::FaceContact: {
                    // Hedef roleKey -> EntityId + role.
                    EntityId targetId = kInvalidEntityId;
                    ComponentInputRole role = ComponentInputRole::None;
                    if (r.to == "main") {
                        targetId = mainId;
                        role = ComponentInputRole::Main;
                    } else if (r.to.rfind("secondary_", 0) == 0) {
                        const std::string suffix =
                            r.to.substr(std::string("secondary_").size());
                        const int idx = suffix.empty() ? -1 : std::atoi(suffix.c_str());
                        if (idx >= 0 && static_cast<std::size_t>(idx) < secondaryIds.size()) {
                            targetId = secondaryIds[static_cast<std::size_t>(idx)];
                            role = ComponentInputRole::Secondary;
                        }
                    } else {
                        // component-owned child hedefi: EntityId henuz yok —
                        // cozum commit'te; distance kisiti icin burada Unresolved.
                        out.error = L"geometrik iliski hedefi child roleKey (prepare'da cozulemez): '" +
                                    std::wstring(r.to.begin(), r.to.end()) + L"'";
                        return out;
                    }
                    if (targetId == kInvalidEntityId) {
                        out.error = L"geometrik iliski hedefi cozulemedi: '" +
                                    std::wstring(r.to.begin(), r.to.end()) + L"'";
                        return out;
                    }
                    if (!resolver) {
                        out.error = L"geometrik iliski icin resolver yok (hedef: '" +
                                    std::wstring(r.to.begin(), r.to.end()) + L"')";
                        return out;
                    }
                    GeometryReference gref;
                    gref.kind = GeometryReferenceKind::MemberFrame;
                    gref.source = targetId;
                    gref.role = role;
                    const ResolvedGeometry rg = resolver->resolve(gref);
                    if (!rg.resolved) {
                        out.error = L"geometrik iliski hedef yuzu cozulemedi (Unresolved): '" +
                                    std::wstring(r.to.begin(), r.to.end()) + L"'";
                        return out;
                    }
                    DistanceConstraint dc;
                    dc.handle = HandleKey{r.from};
                    dc.target.source = targetId;
                    dc.target.role = role;
                    dc.target.faceRole = SemanticFaceRole::None;
                    dc.target.resolved = true;
                    dc.target.plane = rg.plane;
                    dc.signedOffset =
                        (r.kind == RelationshipKind::Offset) ? r.distance : 0.0;
                    dc.enabled = true;
                    sys.addDistanceConstraint(dc);
                    // Dogrulama kaydi: n·p = n·origin + signedOffset.
                    const double d = rg.plane.normal.x * rg.plane.origin.x +
                                     rg.plane.normal.y * rg.plane.origin.y +
                                     rg.plane.normal.z * rg.plane.origin.z;
                    distChecks.push_back({r.from, rg.plane.normal, d + dc.signedOffset});
                    break;
                }
                case RelationshipKind::Parallel:
                case RelationshipKind::Perpendicular:
                case RelationshipKind::FollowFrame: {
                    out.error = L"desteklenmeyen orientation kisiti (OrientationConstraint yok): '" +
                                std::wstring(r.from.begin(), r.from.end()) + L"' -> '" +
                                std::wstring(r.to.begin(), r.to.end()) + L"'";
                    return out;
                }
                default:
                    break;  // ownership/parametric — geometrik dongu DEGIL
            }
        }

        if (!sys.solve()) {
            out.error = L"bagimlilik dongusu (ConstraintSystem Cyclic)";
            return out;
        }
        // Cozum sonrasi: Conflict/Unresolved kisit basari SAYILMAZ (A2.4).
        for (const auto& pc : out.children) {
            const PositionSolveResult pr = sys.position(HandleKey{pc.record.roleKey});
            if (pr.state == ConstraintState::Conflict) {
                out.error = L"geometrik kisit catismasi (Conflict): '" +
                            std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end()) + L"'";
                return out;
            }
            if (pr.state == ConstraintState::Unresolved) {
                out.error = L"cozulemeyen kisit (Unresolved): '" +
                            std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end()) + L"'";
                return out;
            }
        }
        // A2 (rev): URETILEN geometri konumu kisit denklemini saglamali.
        // Child'in hazirlanmis konumu (levha merkezi / bulon orijini / cut
        // duzlemi) n·p = rhs icinde ±1e-3 olmali; degilse generator iliskiyi
        // islememis demektir -> Conflict (sessiz uyumsuzluk yok).
        for (const auto& chk : distChecks) {
            Vec3 pos{};
            bool found = false;
            for (const auto& pc : out.children) {
                if (pc.record.roleKey != chk.roleKey) continue;
                if (pc.record.plate) pos = pc.record.plate->center;
                else if (pc.record.boltGroup) pos = pc.record.boltGroup->origin;
                else if (pc.record.cut) pos = pc.record.cut->planeOrigin;
                found = true;
                break;
            }
            if (!found) continue;
            const double npx = chk.normal.x * pos.x + chk.normal.y * pos.y + chk.normal.z * pos.z;
            if (std::abs(npx - chk.rhs) > 1e-3) {
                out.error = L"geometrik iliski URETILEN geometriyle uyumsuz (Conflict): '" +
                            std::wstring(chk.roleKey.begin(), chk.roleKey.end()) + L"'";
                return out;
            }
        }
    }

    out.ok = true;
    return out;
}

std::vector<std::size_t> ComponentMaterializer::orderChildren(
    const std::vector<PreparedChild>& children, std::wstring& error) {
    // Kahn topolojik sira + SERT kind degismezleri. KIND SIRASI TIE-BREAK DEGIL,
    // KISIT OLARAK EKLENIR: her Plate her BoltGroup'tan ONCE, her BoltGroup her
    // Cut'tan ONCE islenir. Bu degismez materializasyon icin ZORUNLUDUR — bulon
    // grubu sahip oldugu levhayi deler (levha EntityId'si once atanmali), cut
    // hedefini keser (hedef once materialize edilmeli). Explicit bagimlilik
    // (AttachedTo/DependsOn) bu degismezle CELISIRSE (or. plate -> boltGroup)
    // dongu olusur ve plan reddedilir — bu da dogrudur (ters bagimlilik gecersiz).
    const std::size_t n = children.size();
    std::unordered_map<std::string, std::size_t> roleIndex;
    for (std::size_t i = 0; i < n; ++i) roleIndex[children[i].record.roleKey] = i;

    const auto kindRank = [](ComponentObjectKind k) {
        switch (k) {
            case ComponentObjectKind::Plate: return 0;
            case ComponentObjectKind::BoltGroup: return 1;
            case ComponentObjectKind::Cut: return 2;
            case ComponentObjectKind::Weld: return 3;
            default: return 4;
        }
    };

    std::vector<std::set<std::size_t>> in(n);  // in[x] = x'ten sonra islenecekler
    std::vector<std::size_t> indeg(n, 0);
    const auto addEdge = [&](std::size_t from, std::size_t to) {
        if (from == to) return;
        if (in[from].insert(to).second) indeg[to]++;
    };

    // (1) SERT kind degismezleri.
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            if (kindRank(children[i].record.kind) < kindRank(children[j].record.kind))
                addEdge(i, j);

    // (2) Explicit bagimliliklar (AttachedTo/DependsOn): dep -> dependent.
    for (std::size_t i = 0; i < n; ++i)
        for (const auto& dep : children[i].record.dependencies) {
            auto it = roleIndex.find(dep);
            if (it == roleIndex.end()) continue;
            addEdge(it->second, i);
        }

    const auto cmp = [&](std::size_t a, std::size_t b) {
        const int ra = kindRank(children[a].record.kind);
        const int rb = kindRank(children[b].record.kind);
        if (ra != rb) return ra < rb;
        return children[a].record.roleKey < children[b].record.roleKey;
    };

    std::vector<std::size_t> order;
    order.reserve(n);
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < n; ++i)
        if (indeg[i] == 0) ready.push_back(i);
    std::sort(ready.begin(), ready.end(), cmp);

    while (!ready.empty()) {
        const std::size_t cur = ready.front();
        ready.erase(ready.begin());
        order.push_back(cur);
        for (const std::size_t next : in[cur])
            if (--indeg[next] == 0) {
                ready.push_back(next);
                std::sort(ready.begin(), ready.end(), cmp);
            }
    }

    if (order.size() != n) {
        error = L"child bagimlilik/kisit dongusu (materialization sirasi cozulemedi)";
        order.clear();
    }
    return order;
}

ComponentMaterializer::CommitResult ComponentMaterializer::commit(const Prepared& prep,
                                                                  ComponentInstanceRecord record,
                                                                  bool openOwnRecord) {
    CommitResult out;
    if (!prep.ok || !store_) {
        out.error = prep.ok ? L"OCC magaza yok" : prep.error;
        return out;
    }

    // Tek undo kaydi: tum mutasyonlar bu snapshot altinda. Basarisizlikta
    // rollbackOpenRecord() kismi degisikligi geri alir (TD-1'i GENISLETMEZ —
    // redo kaybi raporlanir ama bu dilimde refactor yapilmaz).
    // openOwnRecord=false ise cagiran ZATEN acik bir transaction icindedir;
    // materializer ayni kayda yazar ve rollback'i cagirana birakir.
    if (openOwnRecord) doc_.pushSnapshot();
    const auto rollback = [&]() { if (openOwnRecord) doc_.rollbackOpenRecord(); };

    const std::vector<std::size_t> order = orderChildren(prep.children, out.error);
    if (order.empty() && !prep.children.empty()) {
        rollback();
        out.ok = false;
        return out;
    }

    // roleKey -> EntityId (materialization sirasinda doldurulur).
    // Cut hedefi cozumu (A1.1): "main" -> record.mainPart, "secondary_N" ->
    // record.secondaryParts[N], diger -> out.roleToEntity (component child).
    const auto resolveCutTarget = [&](const PreparedChild& pc) -> EntityId {
        if (pc.cutTargetRole.empty()) return pc.cut.target;  // generator yolu (zaten EntityId)
        if (pc.cutTargetRole == "main") return record.mainPart;
        if (pc.cutTargetRole.rfind("secondary_", 0) == 0) {
            const std::string suffix = pc.cutTargetRole.substr(std::string("secondary_").size());
            const int idx = suffix.empty() ? -1 : std::atoi(suffix.c_str());
            if (idx >= 0 && static_cast<std::size_t>(idx) < record.secondaryParts.size())
                return record.secondaryParts[static_cast<std::size_t>(idx)];
            return kInvalidEntityId;
        }
        const auto it = out.roleToEntity.find(pc.cutTargetRole);
        return it != out.roleToEntity.end() ? it->second : kInvalidEntityId;
    };

    for (const std::size_t ci : order) {
        const PreparedChild& pc = prep.children[ci];
        switch (pc.record.kind) {
            case ComponentObjectKind::Plate: {
                WireframeModel wf = shapeToWireframeWithFaces(pc.solid, 0.15);
                doc_.addModel(std::move(wf));
                const EntityId id = doc_.modelId(doc_.models().size() - 1);
                const VersionId before = store_->activeVersion(id);
                const VersionId after = store_->set(id, pc.solid);
                if (after == kInvalidVersionId) {
                    rollback();
                    out.error = L"levha kati yazilamadi: " +
                                std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end());
                    return out;
                }
                setBaseVersionRecorded(id, after);
                doc_.recordGeometryDelta(id, before, after);
                out.roleToEntity[pc.record.roleKey] = id;
                out.ownedPlateIds.push_back(id);
                break;
            }
            case ComponentObjectKind::BoltGroup: {
                BoltGroup bolt = pc.boltGroup;
                bolt.id = kInvalidEntityId;  // Document atar
                // Parcalar: sahip olunan levhalar (basata) + kullanici parcalari.
                // Generator yalnizca kullanici parcalarini bilir; levhalar burada
                // eklenir (End Plate ile ayni kural: {levha..., main...}).
                std::vector<EntityId> parts;
                for (const EntityId pid : out.ownedPlateIds)
                    if (pid != kInvalidEntityId) parts.push_back(pid);
                for (const EntityId up : bolt.parts)
                    if (up != kInvalidEntityId &&
                        std::find(parts.begin(), parts.end(), up) == parts.end())
                        parts.push_back(up);
                bolt.parts = std::move(parts);

                // Grip: bagli parcalarin eksen boyunca kapsami (levha artık doc'ta).
                const double grip = doc_.boltGripLength(bolt);
                const TopoDS_Shape assembly =
                    buildBoltGroupAssemblyShape(bolt, grip > 0.0 ? grip : 15.0);
                WireframeModel display =
                    assembly.IsNull() ? WireframeModel::cube(10.0)
                                      : shapeToWireframeWithFaces(assembly, 0.15);
                const std::size_t bi = doc_.addBoltGroup(bolt, std::move(display));
                const BoltGroup& created = doc_.boltGroups()[bi];
                if (!assembly.IsNull()) {
                    const VersionId before = store_->activeVersion(created.displayEntityId);
                    const VersionId after = store_->set(created.displayEntityId, assembly);
                    if (after != kInvalidVersionId)
                        doc_.recordGeometryDelta(created.displayEntityId, before, after);
                }
                out.roleToEntity[pc.record.roleKey] = created.id;
                out.ownedBoltGroupIds.push_back(created.id);
                break;
            }
            case ComponentObjectKind::Cut: {
                CutModifier mod = pc.cut;
                mod.target = resolveCutTarget(pc);
                if (mod.target == kInvalidEntityId) {
                    rollback();
                    out.error = L"Cut hedefi EntityId'ye cozulemedi: '" +
                                std::wstring(pc.cutTargetRole.begin(), pc.cutTargetRole.end()) + L"'";
                    return out;
                }
                // Hedef varlik Document'ta mevcut olmali (kesme yapilabilir).
                if (!doc_.indexOfEntity(mod.target)) {
                    rollback();
                    out.error = L"Cut hedefi Document'ta yok: " +
                                std::wstring(pc.cutTargetRole.begin(), pc.cutTargetRole.end());
                    return out;
                }
                mod.enabled = true;
                mod.owner = kInvalidEntityId;  // commit sonrasi component id atanir (asagi)
                out.cutModifierIndices.push_back(doc_.addCutModifier(std::move(mod)));
                out.cutTargets.push_back(mod.target);
                break;
            }
            default:
                // Weld/Construction prepare'da zaten reddedilmisti.
                rollback();
                out.error = L"desteklenmeyen child (commit): " +
                            std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end());
                return out;
        }
    }

    // Child tablosu + owned id'ler kalici kayda yazilir.
    record.children.clear();
    record.ownedPlateIds = out.ownedPlateIds;
    record.ownedBoltGroupIds = out.ownedBoltGroupIds;
    for (const std::size_t ci : order) {
        ComponentChildRecord cr = prep.children[ci].record;
        const auto it = out.roleToEntity.find(cr.roleKey);
        cr.entityId = (it != out.roleToEntity.end()) ? it->second : kInvalidEntityId;
        record.children.push_back(std::move(cr));
    }

    const std::size_t ci = doc_.addComponentInstance(std::move(record));
    out.componentId = doc_.componentInstances()[ci].id;

    // Component-owned cut'lari isaretle (owner = component id) — recompute'ta
    // stale kesmeler bu owner ile kaldirilir (Major b).
    for (const std::size_t cutIdx : out.cutModifierIndices) {
        CutModifier m = doc_.cutModifiers()[cutIdx];
        m.owner = out.componentId;
        doc_.setCutModifier(cutIdx, std::move(m));
    }

    // Kesme zincirini etkilenen parcalara uygula (levha delikleri + kesmeler).
    // A1.2: applyCutsFor basarisizsa (Boolean null / hedef-taban yok) ROLLBACK —
    // sessiz ok=true YASAK.
    for (const EntityId pid : out.ownedPlateIds) {
        if (!applyCutsFor(pid)) {
            rollback();
            out.error = L"levha kesme uygulanamadi (id=" +
                        std::to_wstring(static_cast<std::uint64_t>(pid)) + L")";
            return out;
        }
    }
    for (const EntityId mid : out.ownedBoltGroupIds) {
        const auto bi = doc_.indexOfBoltGroup(mid);
        if (!bi) continue;
        for (const EntityId part : doc_.boltGroups()[*bi].parts) {
            if (!applyCutsFor(part)) {
                rollback();
                out.error = L"bulon deligi uygulanamadi (part id=" +
                            std::to_wstring(static_cast<std::uint64_t>(part)) + L")";
                return out;
            }
        }
    }
    // Component-owned CUT'lar (Fitting/PlaneCut/BoooleanCut) hedef uzerinde de
    // uygulanir — kullanici parcasina kesme bulon grubuna bagli olmayabilir.
    for (const EntityId ct : out.cutTargets) {
        if (ct == kInvalidEntityId) continue;
        if (!applyCutsFor(ct)) {
            rollback();
            out.error = L"cut hedefi kesilemedi (id=" +
                        std::to_wstring(static_cast<std::uint64_t>(ct)) + L")";
            return out;
        }
    }

    out.ok = true;
    return out;
}

ComponentMaterializer::CommitResult ComponentMaterializer::recompute(
    const Prepared& prep, const ComponentInstanceRecord& existing,
    ComponentInstanceRecord record, bool openOwnRecord) {
    CommitResult out;
    if (!prep.ok || !store_) {
        out.error = prep.ok ? L"OCC magaza yok" : prep.error;
        return out;
    }

    // roleKey -> EntityId eslesmesi MEVCUT kayittan (children tablosu) okunur.
    // Yeni eklenen child'lar icin kInvalidEntityId kalir -> yeni id atanir.
    std::unordered_map<std::string, EntityId> stable;
    for (const auto& c : existing.children) stable[c.roleKey] = c.entityId;

    if (openOwnRecord) doc_.pushSnapshot();
    const auto rollback = [&]() { if (openOwnRecord) doc_.rollbackOpenRecord(); };

    const std::vector<std::size_t> order = orderChildren(prep.children, out.error);
    if (order.empty() && !prep.children.empty()) {
        rollback();
        out.ok = false;
        return out;
    }

    // Cut hedefi cozumu (A1.1): "main" -> record.mainPart, "secondary_N" ->
    // record.secondaryParts[N], diger -> out.roleToEntity (component child).
    const auto resolveCutTarget = [&](const PreparedChild& pc) -> EntityId {
        if (pc.cutTargetRole.empty()) return pc.cut.target;
        if (pc.cutTargetRole == "main") return record.mainPart;
        if (pc.cutTargetRole.rfind("secondary_", 0) == 0) {
            const std::string suffix = pc.cutTargetRole.substr(std::string("secondary_").size());
            const int idx = suffix.empty() ? -1 : std::atoi(suffix.c_str());
            if (idx >= 0 && static_cast<std::size_t>(idx) < record.secondaryParts.size())
                return record.secondaryParts[static_cast<std::size_t>(idx)];
            return kInvalidEntityId;
        }
        const auto it = out.roleToEntity.find(pc.cutTargetRole);
        return it != out.roleToEntity.end() ? it->second : kInvalidEntityId;
    };

    // A1.4: ARTIK URETILMEYEN child'lari kaldir. Yeni planda roleKey'i olmayan
    // eski owned nesneler (levha/bulon grubu/cut) silinir; kullanici parcalarina
    // (main/secondary) DOKUNULMAZ. Bu islem ayni transaction icinde, geri alinabilir.
    {
        std::set<std::string> newKeys;
        for (const auto& pc : prep.children) newKeys.insert(pc.record.roleKey);
        for (const auto& c : existing.children) {
            if (newKeys.count(c.roleKey)) continue;  // hala uretiliyor
            switch (c.kind) {
                case ComponentObjectKind::Plate: {
                    // OWNERSHIP DOGRULAMA: yalnizca component-owned levhalar silinir.
                    const bool owned = std::find(existing.ownedPlateIds.begin(),
                                                 existing.ownedPlateIds.end(),
                                                 c.entityId) != existing.ownedPlateIds.end();
                    if (!owned || c.entityId == kInvalidEntityId) continue;
                    if (const auto pi = doc_.indexOfEntity(c.entityId)) {
                        // YAPRAK sil (kaskad YOK): deleteModels bulon kaskadini
                        // tetikleyip HÂLÂ yasayan bir bulon grubunu silebilirdi.
                        doc_.removeModelLeaf(*pi);
                    }
                    break;
                }
                case ComponentObjectKind::BoltGroup: {
                    const bool owned = std::find(existing.ownedBoltGroupIds.begin(),
                                                 existing.ownedBoltGroupIds.end(),
                                                 c.entityId) != existing.ownedBoltGroupIds.end();
                    if (!owned || c.entityId == kInvalidEntityId) continue;
                    if (const auto bi = doc_.indexOfBoltGroup(c.entityId))
                        doc_.removeBoltGroup(*bi);
                    break;
                }
                case ComponentObjectKind::Cut:
                    // Component-owned cut'lar ASAGIDA owner-takibiyle kaldirilir
                    // (yeni plandaki cut'lar yeniden eklenmeden ONCE). Burada
                    // islem yapilmaz — cut EntityId tasimaz.
                    break;
                default:
                    break;  // weld/construction — sahiplik yok
            }
        }
    }

    // A1.4/Major(b): component-owned CUT modifier'larini kaldir (owner == component
    // id). Yeni plandaki cut'lar yeniden eklenecek; eski/değişmiş kesmeler birikmez.
    {
        for (std::size_t ci = doc_.cutModifiers().size(); ci-- > 0;) {
            if (doc_.cutModifiers()[ci].owner == existing.id) doc_.removeCutModifier(ci);
        }
    }

    for (const std::size_t ci : order) {
        const PreparedChild& pc = prep.children[ci];
        const auto stableIt = stable.find(pc.record.roleKey);
        const bool hasStable = stableIt != stable.end() && stableIt->second != kInvalidEntityId;
        const EntityId stableId = hasStable ? stableIt->second : kInvalidEntityId;

        switch (pc.record.kind) {
            case ComponentObjectKind::Plate: {
                WireframeModel wf = shapeToWireframeWithFaces(pc.solid, 0.15);
                if (hasStable && doc_.indexOfEntity(stableId)) {
                    // Kimlik korunur: replaceModel + ayni EntityId'ye yeni kati.
                    doc_.replaceModel(*doc_.indexOfEntity(stableId), {std::move(wf)});
                    const VersionId before = store_->activeVersion(stableId);
                    const VersionId after = store_->set(stableId, pc.solid);
                    if (after == kInvalidVersionId) {
                        rollback();
                        out.error = L"levha kati guncellenemedi: " +
                                    std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end());
                        return out;
                    }
                    setBaseVersionRecorded(stableId, after);
                    doc_.recordGeometryDelta(stableId, before, after);
                    out.roleToEntity[pc.record.roleKey] = stableId;
                    out.ownedPlateIds.push_back(stableId);
                } else {
                    doc_.addModel(std::move(wf));
                    const EntityId id = doc_.modelId(doc_.models().size() - 1);
                    const VersionId before = store_->activeVersion(id);
                    const VersionId after = store_->set(id, pc.solid);
                    if (after == kInvalidVersionId) {
                        rollback();
                        out.error = L"levha kati yazilamadi: " +
                                    std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end());
                        return out;
                    }
                    setBaseVersionRecorded(id, after);
                    doc_.recordGeometryDelta(id, before, after);
                    out.roleToEntity[pc.record.roleKey] = id;
                    out.ownedPlateIds.push_back(id);
                }
                break;
            }
            case ComponentObjectKind::BoltGroup: {
                BoltGroup bolt = pc.boltGroup;
                std::vector<EntityId> parts;
                for (const EntityId pid : out.ownedPlateIds)
                    if (pid != kInvalidEntityId) parts.push_back(pid);
                for (const EntityId up : bolt.parts)
                    if (up != kInvalidEntityId &&
                        std::find(parts.begin(), parts.end(), up) == parts.end())
                        parts.push_back(up);
                bolt.parts = std::move(parts);

                const double grip = doc_.boltGripLength(bolt);
                const TopoDS_Shape assembly =
                    buildBoltGroupAssemblyShape(bolt, grip > 0.0 ? grip : 15.0);
                WireframeModel display = assembly.IsNull()
                                             ? WireframeModel::cube(10.0)
                                             : shapeToWireframeWithFaces(assembly, 0.15);

                if (hasStable && doc_.indexOfBoltGroup(stableId)) {
                    bolt.id = stableId;
                    doc_.setBoltGroup(*doc_.indexOfBoltGroup(stableId), bolt);
                    const BoltGroup& updated = doc_.boltGroups()[*doc_.indexOfBoltGroup(stableId)];
                    if (!assembly.IsNull()) {
                        const VersionId before = store_->activeVersion(updated.displayEntityId);
                        const VersionId after = store_->set(updated.displayEntityId, assembly);
                        if (after != kInvalidVersionId)
                            doc_.recordGeometryDelta(updated.displayEntityId, before, after);
                    }
                    out.roleToEntity[pc.record.roleKey] = updated.id;
                    out.ownedBoltGroupIds.push_back(updated.id);
                } else {
                    bolt.id = kInvalidEntityId;
                    const std::size_t bi = doc_.addBoltGroup(bolt, std::move(display));
                    const BoltGroup& created = doc_.boltGroups()[bi];
                    if (!assembly.IsNull()) {
                        const VersionId before = store_->activeVersion(created.displayEntityId);
                        const VersionId after = store_->set(created.displayEntityId, assembly);
                        if (after != kInvalidVersionId)
                            doc_.recordGeometryDelta(created.displayEntityId, before, after);
                    }
                    out.roleToEntity[pc.record.roleKey] = created.id;
                    out.ownedBoltGroupIds.push_back(created.id);
                }
                break;
            }
            case ComponentObjectKind::Cut: {
                CutModifier mod = pc.cut;
                mod.target = resolveCutTarget(pc);
                if (mod.target == kInvalidEntityId) {
                    rollback();
                    out.error = L"Cut hedefi EntityId'ye cozulemedi: '" +
                                std::wstring(pc.cutTargetRole.begin(), pc.cutTargetRole.end()) + L"'";
                    return out;
                }
                if (!doc_.indexOfEntity(mod.target)) {
                    rollback();
                    out.error = L"Cut hedefi Document'ta yok: " +
                                std::wstring(pc.cutTargetRole.begin(), pc.cutTargetRole.end());
                    return out;
                }
                mod.enabled = true;
                mod.owner = existing.id;  // component-owned cut (recompute)
                out.cutTargets.push_back(mod.target);
                doc_.addCutModifier(std::move(mod));
                break;
            }
            default:
                rollback();
                out.error = L"desteklenmeyen child (recompute): " +
                            std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end());
                return out;
        }
    }

    // Child tablosu + owned id'ler guncellenir; record kimligi korunur.
    record.children.clear();
    record.ownedPlateIds = out.ownedPlateIds;
    record.ownedBoltGroupIds = out.ownedBoltGroupIds;
    for (const std::size_t ci : order) {
        ComponentChildRecord cr = prep.children[ci].record;
        const auto it = out.roleToEntity.find(cr.roleKey);
        cr.entityId = (it != out.roleToEntity.end()) ? it->second : kInvalidEntityId;
        record.children.push_back(std::move(cr));
    }

    const auto idx = doc_.indexOfComponentInstance(existing.id);
    if (!idx) {
        rollback();
        out.error = L"component instance bulunamadi (recompute)";
        return out;
    }
    doc_.setComponentInstance(*idx, std::move(record));
    out.componentId = existing.id;

    for (const EntityId pid : out.ownedPlateIds) {
        if (!applyCutsFor(pid)) {
            rollback();
            out.error = L"recompute: levha kesme uygulanamadi (id=" +
                        std::to_wstring(static_cast<std::uint64_t>(pid)) + L")";
            return out;
        }
    }
    for (const EntityId mid : out.ownedBoltGroupIds) {
        const auto bi = doc_.indexOfBoltGroup(mid);
        if (!bi) continue;
        for (const EntityId part : doc_.boltGroups()[*bi].parts) {
            if (!applyCutsFor(part)) {
                rollback();
                out.error = L"recompute: bulon deligi uygulanamadi (part id=" +
                            std::to_wstring(static_cast<std::uint64_t>(part)) + L")";
                return out;
            }
        }
    }
    // Component-owned CUT'lar hedef uzerinde de uygulanir (Major a).
    for (const EntityId ct : out.cutTargets) {
        if (ct == kInvalidEntityId) continue;
        if (!applyCutsFor(ct)) {
            rollback();
            out.error = L"recompute: cut hedefi kesilemedi (id=" +
                        std::to_wstring(static_cast<std::uint64_t>(ct)) + L")";
            return out;
        }
    }

    out.ok = true;
    return out;
}

void ComponentMaterializer::setBaseVersionRecorded(EntityId id, VersionId version) {
    if (!store_) return;
    const VersionId oldBase = store_->baseVersion(id);
    store_->setBaseVersion(id, version);
    if (oldBase != version)
        doc_.recordBaseVersionDelta(id, oldBase, version);
}

bool ComponentMaterializer::applyCutsFor(EntityId target) {
    if (!store_ || target == kInvalidEntityId) return false;
    const auto idxOpt = doc_.indexOfEntity(target);
    if (!idxOpt) return false;
    const std::size_t index = *idxOpt;

    VersionId base = store_->baseVersion(target);
    if (base == kInvalidVersionId) {
        base = store_->activeVersion(target);
        if (base == kInvalidVersionId) return false;
        setBaseVersionRecorded(target, base);
    }
    const TopoDS_Shape* baseShape = store_->findVersion(target, base);
    if (!baseShape) return false;

    const EntityProperties& props = doc_.models()[index].properties();
    const Vec3 axisFrom{props.axisFromX, props.axisFromY, props.axisFromZ};
    const Vec3 axisTo{props.axisToX, props.axisToY, props.axisToZ};

    std::vector<CutModifier> mods;
    for (const std::size_t ci : doc_.cutModifiersForTarget(target))
        mods.push_back(resolveCutModifierSource(doc_.cutModifiers()[ci], doc_));

    const ToolShapeResolver resolveTool = [&](EntityId id) -> const TopoDS_Shape* {
        return store_->find(id);
    };
    const TopoDS_Shape result =
        applyCutModifierChain(*baseShape, mods, axisFrom, axisTo, resolveTool);
    if (result.IsNull()) return false;  // Boolean failure — sessiz basari YOK

    auto cutModel = shapeToWireframeWithFaces(result, 0.15);
    EntityProperties cutProps = props;
    cutModel.setProperties(std::move(cutProps));
    doc_.replaceModel(index, {std::move(cutModel)});
    const VersionId before = store_->activeVersion(target);
    const VersionId after = store_->set(target, result);
    if (after == kInvalidVersionId) return false;
    doc_.recordGeometryDelta(target, before, after);
    return true;
}

}  // namespace mm
#endif  // MM_HAS_OCC
