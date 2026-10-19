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
    const std::vector<EntityId>& secondaryIds) const {
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
                // Hedef cozumu commit'te (EntityId ataninca) yapilir; burada
                // yalnizca modifier gecerliligi denetlenir.
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
                            std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end()) + L"')";
                return out;
            }
        }
    }

    out.ok = true;
    return out;
}

std::vector<std::size_t> ComponentMaterializer::orderChildren(
    const std::vector<PreparedChild>& children, std::wstring& error) {
    // Kahn topolojik sira; esitlikte lexicographic roleKey (deterministik).
    // dependencies roleKey -> indeks; yalnizca child'lar arasi kenarlar.
    std::unordered_map<std::string, std::size_t> roleIndex;
    for (std::size_t i = 0; i < children.size(); ++i)
        roleIndex[children[i].record.roleKey] = i;

    std::vector<std::set<std::size_t>> in(children.size());
    std::vector<std::size_t> indeg(children.size(), 0);
    for (std::size_t i = 0; i < children.size(); ++i) {
        for (const auto& dep : children[i].record.dependencies) {
            auto it = roleIndex.find(dep);
            if (it == roleIndex.end()) continue;  // rezerve/user part -> kenar yok
            if (it->second == i) continue;        // oz bagimlilik yok sayilir
            in[it->second].insert(i);             // dep -> i (dep once)
            indeg[i]++;
        }
    }

    // Hazir dugumler (indeg 0) lexicographic roleKey sirada.
    std::vector<std::size_t> order;
    order.reserve(children.size());
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < children.size(); ++i)
        if (indeg[i] == 0) ready.push_back(i);
    std::sort(ready.begin(), ready.end(), [&](std::size_t a, std::size_t b) {
        return children[a].record.roleKey < children[b].record.roleKey;
    });

    while (!ready.empty()) {
        const std::size_t cur = ready.front();
        ready.erase(ready.begin());
        order.push_back(cur);
        for (const std::size_t next : in[cur]) {
            if (--indeg[next] == 0) {
                ready.push_back(next);
                std::sort(ready.begin(), ready.end(), [&](std::size_t a, std::size_t b) {
                    return children[a].record.roleKey < children[b].record.roleKey;
                });
            }
        }
    }

    if (order.size() != children.size()) {
        error = L"child bagimlilik dongusu (materialization sirasi cozulemedi)";
        order.clear();
    }
    return order;
}

ComponentMaterializer::CommitResult ComponentMaterializer::commit(const Prepared& prep,
                                                                  ComponentInstanceRecord record) {
    CommitResult out;
    if (!prep.ok || !store_) {
        out.error = prep.ok ? L"OCC magaza yok" : prep.error;
        return out;
    }

    // Tek undo kaydi: tum mutasyonlar bu snapshot altinda. Basarisizlikta
    // rollbackOpenRecord() kismi degisikligi geri alir (TD-1'i GENISLETMEZ —
    // redo kaybi raporlanir ama bu dilimde refactor yapilmaz).
    doc_.pushSnapshot();

    const std::vector<std::size_t> order = orderChildren(prep.children, out.error);
    if (order.empty() && !prep.children.empty()) {
        doc_.rollbackOpenRecord();
        out.ok = false;
        return out;
    }

    // roleKey -> EntityId (materialization sirasinda doldurulur).
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
                    doc_.rollbackOpenRecord();
                    out.error = L"levha kati yazilamadi: " +
                                std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end());
                    return out;
                }
                store_->setBaseVersion(id, after);
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
                // Hedef cozumu: target roleKey -> EntityId (materialize edilmis
                // child veya kullanici parcasi). Girdide zaten EntityId varsa
                // (generator yolu) korunur.
                mod.enabled = true;
                doc_.addCutModifier(std::move(mod));
                break;
            }
            default:
                // Weld/Construction prepare'da zaten reddedilmisti.
                doc_.rollbackOpenRecord();
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

    // Kesme zincirini etkilenen parcalara uygula (levha delikleri + kesmeler).
    for (const EntityId pid : out.ownedPlateIds) applyCutsFor(pid);
    for (const EntityId mid : out.ownedBoltGroupIds) {
        const auto bi = doc_.indexOfBoltGroup(mid);
        if (!bi) continue;
        for (const EntityId part : doc_.boltGroups()[*bi].parts) applyCutsFor(part);
    }

    out.ok = true;
    return out;
}

void ComponentMaterializer::applyCutsFor(EntityId target) {
    if (!store_ || target == kInvalidEntityId) return;
    const auto idxOpt = doc_.indexOfEntity(target);
    if (!idxOpt) return;
    const std::size_t index = *idxOpt;

    VersionId base = store_->baseVersion(target);
    if (base == kInvalidVersionId) {
        base = store_->activeVersion(target);
        if (base == kInvalidVersionId) return;
        store_->setBaseVersion(target, base);
        doc_.recordBaseVersionDelta(target, kInvalidVersionId, base);
    }
    const TopoDS_Shape* baseShape = store_->findVersion(target, base);
    if (!baseShape) return;

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
    if (result.IsNull()) return;

    auto cutModel = shapeToWireframeWithFaces(result, 0.15);
    EntityProperties cutProps = props;
    cutModel.setProperties(std::move(cutProps));
    doc_.replaceModel(index, {std::move(cutModel)});
    const VersionId before = store_->activeVersion(target);
    const VersionId after = store_->set(target, result);
    if (after != kInvalidVersionId) doc_.recordGeometryDelta(target, before, after);
}

}  // namespace mm
#endif  // MM_HAS_OCC
