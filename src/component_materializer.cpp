#ifdef MM_HAS_OCC
#include "model_maker/component_materializer.hpp"

#include "model_maker/bolt.hpp"
#include "model_maker/bolt_geometry.hpp"
#include "model_maker/cut_modifier.hpp"
#include "model_maker/weld.hpp"
#include "model_maker/weld_geometry.hpp"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// Renk konvansiyonu (Tekla tarzi): levha gri, civata (bolon) kahverengi,
// kaynak kati parlak yesil (W7: 4m kolonda 5mm kaynak uzaktan gorunur olsun).
constexpr std::uint32_t kPlateColor = 0x808080u;  // gri
constexpr std::uint32_t kBoltColor = 0x8B592Au;   // kahverengi (RGB 139,89,42)
constexpr std::uint32_t kWeldColor = 0x00C853u;   // parlak yesil

// Tekla parca ozellikleri: Name / Material / Finish / Pos_No (-> description).
// Bos alan mevcut degere DOKUNMAZ.
void applyPlateProps(WireframeModel& model, const PlateSpec& spec) noexcept {
    const auto narrow = [](const std::wstring& w) {
        std::string n;
        n.reserve(w.size());
        for (const wchar_t ch : w) n.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
        return n;
    };
    auto props = model.properties();
    if (!spec.name.empty()) props.name = narrow(spec.name);
    if (!spec.material.empty()) props.material = narrow(spec.material);
    if (!spec.finish.empty()) props.finish = narrow(spec.finish);
    if (!spec.posNo.empty()) props.description = "Pos_No: " + narrow(spec.posNo);
    model.setProperties(std::move(props));
}

void applyColor(WireframeModel& model, std::uint32_t color) noexcept {
    auto props = model.properties();
    props.trueColor = color;
    props.effectiveColor = color;
    model.setProperties(std::move(props));
}

// Bir weld kaydi icin (main/sec B-Rep'ten) fillet kati + display modeli uretir.
// Basariliysa solidOut/wfOut doldurulur ve w.geometrySupported=true; degilse
// w.unsupportedReason'a neden yazilir ve false doner (SESSIZ yanlis kati YOK).
bool buildWeldGeometry(Weld& w, const TopoDS_Shape& mainShape,
                       const TopoDS_Shape& secShape, TopoDS_Shape& solidOut,
                       WireframeModel& wfOut) {
    const double leg = w.sizeIsThroat ? w.size * std::sqrt(2.0) : w.size;
    std::string reason;
    double seam = 0.0;
    const auto solids = buildWeldSolids(w, mainShape, secShape, leg, reason, &seam);
    if (seam > 0.0) w.length = seam;
    if (solids.empty()) {
        w.unsupportedReason = reason.empty() ? "kaynak kati uretilemedi" : reason;
        return false;
    }
    // Fuse YOK: dikis katilari compound'da toplanir (Windows OCCT 7.9.3-3 Fuse
    // cokmesi, bkz. compoundOfShapes).
    const TopoDS_Shape fused = compoundOfShapes(solids);
    if (fused.IsNull()) {
        w.unsupportedReason = "kaynak kati birlestirilemedi";
        return false;
    }
    const WeldJunction j = findBestWeldJunction(mainShape, secShape, 0.5);
    if (j.valid) {
        w.origin = j.start;
        w.direction = j.direction;
        w.mainNormal = j.mainNormal;
        w.secNormal = j.secNormal;
    }
    w.geometrySupported = true;
    w.unsupportedReason.clear();
    WireframeModel wf = shapeToWireframeWithFaces(fused, 0.15);
    applyColor(wf, kWeldColor);
    solidOut = fused;
    wfOut = std::move(wf);
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
    // (su, sv) yerel koordinat (mm, levha merkezine gore) -> dunya noktasi.
    const auto pt = [&](double lu, double lv) {
        return base.Translated(gp_Vec(uu) * lu + gp_Vec(vv) * lv);
    };

    // SERBEST KONTUR (egik kiris haunch govdesi): noktalar levha merkezine
    // gore (u,v). Dikdortgen+pah yolu atlanir; tel -> yuzey -> prizma.
    if (!p.outline.empty()) {
        if (p.outline.size() < 3) return TopoDS_Shape();
        BRepBuilderAPI_MakePolygon poly;
        for (const auto& q : p.outline) poly.Add(pt(q.first, q.second));
        poly.Close();
        if (!poly.IsDone()) return TopoDS_Shape();
        BRepBuilderAPI_MakeFace face(poly.Wire());
        if (!face.IsDone()) return TopoDS_Shape();
        return BRepPrimAPI_MakePrism(face.Face(), gp_Vec(z) * p.thickness).Shape();
    }

    // Pahsiz levha: eski dogrudan poligon yolu (davranis degismez).
    if (p.chamfers.empty()) {
        BRepBuilderAPI_MakePolygon poly;
        poly.Add(pt(-hw, -hh));
        poly.Add(pt(hw, -hh));
        poly.Add(pt(hw, hh));
        poly.Add(pt(-hw, hh));
        poly.Close();
        BRepBuilderAPI_MakeFace face(poly.Wire());
        if (!face.IsDone()) return TopoDS_Shape();
        return BRepPrimAPI_MakePrism(face.Face(), gp_Vec(z) * p.thickness).Shape();
    }

    // PAHLI levha: kontur 4 kose; pahli kose iki noktaya (+ yay orta noktasina) bolunur.
    // Boolean YOK (tel -> yuzey -> prizma). Bacaklar levha yarisini gecmeyecek sekilde
    // kirpilir (komsu pahlar cakismasin).
    struct Cn { double sx, sy; };
    const Cn corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    struct Seg { gp_Pnt a; gp_Pnt mid; gp_Pnt b; bool arc; };
    std::vector<gp_Pnt> verts;               // sirali kontur noktalari
    std::vector<bool> arcAfter;              // verts[i] -> verts[i+1] yay mi
    std::vector<gp_Pnt> arcMid;              // yay orta noktasi (arc ise)
    const auto findChamfer = [&](int idx) -> const PlateSpec::Chamfer* {
        for (const auto& ch : p.chamfers)
            if (ch.corner == idx && ch.h > 1e-9 && ch.v > 1e-9) return &ch;
        return nullptr;
    };
    for (int i = 0; i < 4; ++i) {
        const Cn& cn = corners[i];
        const PlateSpec::Chamfer* ch = findChamfer(i);
        if (!ch) {
            verts.push_back(pt(cn.sx * hw, cn.sy * hh));
            arcAfter.push_back(false);
            arcMid.push_back(gp_Pnt());
            continue;
        }
        // A single chamfer can describe an almost full-edge tapered haunch
        // web. Only multiple adjacent corner chamfers need the legacy half-
        // dimension overlap cap. Leave tiny material at the far web tip.
        const double maxFraction = (p.chamfers.size() == 1) ? 0.98 : 0.5;
        const double a = std::min(ch->h, p.width * maxFraction);
        const double b = std::min(ch->v, p.height * maxFraction);
        // Kontur saat yonunun tersi (0->1->2->3). Kosede gelen kenar ve giden kenar:
        // gelen nokta kosenin "onceki" kenarinda, giden nokta "sonraki" kenarinda.
        // Kose (cu,cv); ic yon (-sx,-sy). P_u: u-kenari uzerinde (cu - sx*a, cv),
        // P_v: v-kenari uzerinde (cu, cv - sy*b).
        const double cu = cn.sx * hw, cv = cn.sy * hh;
        const gp_Pnt pu = pt(cu - cn.sx * a, cv);
        const gp_Pnt pv = pt(cu, cv - cn.sy * b);
        // Kontur yonu: i=0:(-,-) -> 1:(+,-) -> 2:(+,+) -> 3:(-,+): dolasim sagin tersi.
        // Kose 0 ve 2'de gelen kenar v-kenari, giden u-kenari; 1 ve 3'te tersi.
        const bool incomingIsV = (i == 0 || i == 2);
        const gp_Pnt first = incomingIsV ? pv : pu;
        const gp_Pnt second = incomingIsV ? pu : pv;
        // Yay orta noktasi: ic merkez C = (cu - sx*a, cv - sy*b); konveks = C +
        // (sx*a, sy*b)*cos45 ; konkav (merkez koseye) = kose + (-sx*a, -sy*b)*cos45.
        const double k = 0.70710678118654752;
        gp_Pnt mid;
        if (ch->type == 2)
            mid = pt(cu - cn.sx * a + cn.sx * a * k, cv - cn.sy * b + cn.sy * b * k);
        else
            mid = pt(cu - cn.sx * a * (1.0 - k), cv - cn.sy * b * (1.0 - k));
        // not: konkav icin kose merkezli yay; orta nokta kose + (-s*a*k, -s*b*k) =
        // cu - sx*a*k ... asagida duzeltilir.
        if (ch->type == 3) mid = pt(cu - cn.sx * a * k, cv - cn.sy * b * k);
        verts.push_back(first);
        arcAfter.push_back(ch->type == 2 || ch->type == 3);
        arcMid.push_back(mid);
        verts.push_back(second);
        arcAfter.push_back(false);
        arcMid.push_back(gp_Pnt());
    }
    BRepBuilderAPI_MakeWire wire;
    const std::size_t n = verts.size();
    for (std::size_t i = 0; i < n; ++i) {
        const gp_Pnt& a = verts[i];
        const gp_Pnt& b = verts[(i + 1) % n];
        if (a.Distance(b) < 1e-7) continue;
        if (arcAfter[i]) {
            GC_MakeArcOfCircle arc(a, arcMid[i], b);
            if (!arc.IsDone()) return TopoDS_Shape();
            wire.Add(BRepBuilderAPI_MakeEdge(arc.Value()).Edge());
        } else {
            wire.Add(BRepBuilderAPI_MakeEdge(a, b).Edge());
        }
    }
    if (!wire.IsDone()) return TopoDS_Shape();
    BRepBuilderAPI_MakeFace face(wire.Wire());
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
            case ComponentObjectKind::Weld: {
                if (!child.weld) {
                    out.error = L"Weld child payload eksik: " +
                                std::wstring(child.roleKey.begin(), child.roleKey.end());
                    return out;
                }
                // KAYNAK PARCA COZUMU commit/recompute'ta yapilir (owned levha
                // EntityId'si henuz atanmamis). Burada yalnizca referans
                // gecerliligi dogrulanir: her iki taraf da EntityId VEYA roleKey
                // tasimali. Yetersiz referans -> acik Unsupported (sessiz basari
                // URETILMEZ — C2 kurali).
                const WeldSpec& ws = *child.weld;
                const bool aOk = ws.partA != kInvalidEntityId || !ws.partARole.empty();
                const bool bOk = ws.partB != kInvalidEntityId || !ws.partBRole.empty();
                if (!aOk || !bOk) {
                    out.error = L"Weld parcasi cozulemedi (partA/partB EntityId veya roleKey yok): '" +
                                std::wstring(child.roleKey.begin(), child.roleKey.end()) + L"'";
                    return out;
                }
                if (ws.size <= 0.0 && ws.throat <= 0.0) {
                    out.error = L"Weld olcusu <= 0: '" +
                                std::wstring(child.roleKey.begin(), child.roleKey.end()) + L"'";
                    return out;
                }
                break;
            }
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

    // Kaynak parcasi cozumu: EntityId verilmisse aynen; roleKey verilmisse
    // \"main\"/\"secondary_N\" rezerve rol veya owned child (out.roleToEntity).
    const auto resolveWeldPart = [&](EntityId direct, const std::string& role) -> EntityId {
        if (direct != kInvalidEntityId) return direct;
        if (role == "main") return record.mainPart;
        if (role.rfind("secondary_", 0) == 0) {
            const std::string suffix = role.substr(std::string("secondary_").size());
            const int idx = suffix.empty() ? -1 : std::atoi(suffix.c_str());
            if (idx >= 0 && static_cast<std::size_t>(idx) < record.secondaryParts.size())
                return record.secondaryParts[static_cast<std::size_t>(idx)];
            return kInvalidEntityId;
        }
        const auto it = out.roleToEntity.find(role);
        return it != out.roleToEntity.end() ? it->second : kInvalidEntityId;
    };

    // Kaynak kimligi (\"W001\", ...) — mevcut kaynak sayisindan turetilir.
    const auto nextWeldIdentification = [&]() {
        std::uint64_t seq = doc_.welds().size() + 1;
        std::string digits = std::to_string(seq);
        while (digits.size() < 3) digits = "0" + digits;
        std::string id = "W";
        id += digits;
        return id;
    };

    for (const std::size_t ci : order) {
        const PreparedChild& pc = prep.children[ci];
        switch (pc.record.kind) {
            case ComponentObjectKind::Plate: {
                WireframeModel wf = shapeToWireframeWithFaces(pc.solid, 0.15);
                applyColor(wf, kPlateColor);
                if (pc.record.plate) applyPlateProps(wf, *pc.record.plate);
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
                if (!bolt.targetOwnedPlateRole.empty()) {
                    const auto role = out.roleToEntity.find(bolt.targetOwnedPlateRole);
                    if (role == out.roleToEntity.end() ||
                        std::find(out.ownedPlateIds.begin(), out.ownedPlateIds.end(),
                                  role->second) == out.ownedPlateIds.end()) {
                        rollback();
                        out.error = L"bulonun hedef owned levha rolu bulunamadi: " +
                            std::wstring(bolt.targetOwnedPlateRole.begin(),
                                         bolt.targetOwnedPlateRole.end());
                        return out;
                    }
                    parts.push_back(role->second);
                } else {
                    // Legacy generator: all owned plates remain in the stack.
                    for (const EntityId pid : out.ownedPlateIds)
                        if (pid != kInvalidEntityId) parts.push_back(pid);
                }
                for (const EntityId up : bolt.parts)
                    if (up != kInvalidEntityId &&
                        std::find(parts.begin(), parts.end(), up) == parts.end())
                        parts.push_back(up);
                bolt.parts = std::move(parts);
                resolveHoleShapeParts(bolt, out.ownedPlateIds);  // Tekla Slots in / Hole type

                // Grip: oncelikle generator'in gripLength'i (levha + web); yoksa
                // holeDepth; yoksa bagli parcalarin bbox kapsami (I-profil flans
                // genisligini grip'e sokabilir — son care).
                const double grip =
                    (bolt.gripLength > 0.0) ? bolt.gripLength
                    : (bolt.holeDepth > 0.0) ? bolt.holeDepth
                    : doc_.boltGripLength(bolt);
                const TopoDS_Shape assembly =
                    buildBoltGroupAssemblyShape(bolt, grip > 0.0 ? grip : 15.0);
                WireframeModel display =
                    assembly.IsNull() ? WireframeModel::cube(10.0)
                                      : shapeToWireframeWithFaces(assembly, 0.15);
                applyColor(display, kBoltColor);
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
            case ComponentObjectKind::Weld: {
                const WeldSpec& spec = *pc.record.weld;
                const EntityId partA = resolveWeldPart(spec.partA, spec.partARole);
                const EntityId partB = resolveWeldPart(spec.partB, spec.partBRole);
                if (partA == kInvalidEntityId || partB == kInvalidEntityId || partA == partB) {
                    rollback();
                    out.error = L"Weld parcasi cozulemedi: '" +
                                std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end()) + L"'";
                    return out;
                }
                const auto* shapeA = store_->find(partA);
                const auto* shapeB = store_->find(partB);
                if (!shapeA || !shapeB) {
                    rollback();
                    out.error = L"Weld parcasi B-Rep yok: '" +
                                std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end()) + L"'";
                    return out;
                }
                Weld w;
                w.mainPart = partA;
                w.secondaryPart = partB;
                w.type = WeldType::Fillet;
                w.size = spec.size > 0.0 ? spec.size : spec.throat;
                w.doubleSided = spec.doubleSided;
                w.weldClass = WeldClass::Shop;
                w.identification = nextWeldIdentification();
                TopoDS_Shape solid;
                WireframeModel wf;
                if (buildWeldGeometry(w, *shapeA, *shapeB, solid, wf)) {
                    const std::size_t wi = doc_.addWeld(std::move(w), std::move(wf));
                    const Weld& created = doc_.welds()[wi];
                    if (created.displayEntityId != kInvalidEntityId) {
                        const VersionId before = store_->activeVersion(created.displayEntityId);
                        const VersionId after = store_->set(created.displayEntityId, solid);
                        if (after != kInvalidVersionId)
                            doc_.recordGeometryDelta(created.displayEntityId, before, after);
                    }
                    out.roleToEntity[pc.record.roleKey] = created.id;
                } else {
                    // Geometri desteklenmiyor: kayit yine eklenir (metadata/glif) —
                    // ama SESSIZ degil; unsupportedReason korunur (kullanici gorur).
                    w.unsupportedReason = w.unsupportedReason.empty()
                                               ? "kaynak kati uretilemedi"
                                               : w.unsupportedReason;
                    const std::size_t wi = doc_.addWeld(std::move(w));
                    out.roleToEntity[pc.record.roleKey] = doc_.welds()[wi].id;
                }
                break;
            }
            default:
                // Construction prepare'da zaten reddedilmisti.
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

    // Kaynak parcasi cozumu (commit'teki ile ayni).
    const auto resolveWeldPart = [&](EntityId direct, const std::string& role) -> EntityId {
        if (direct != kInvalidEntityId) return direct;
        if (role == "main") return record.mainPart;
        if (role.rfind("secondary_", 0) == 0) {
            const std::string suffix = role.substr(std::string("secondary_").size());
            const int idx = suffix.empty() ? -1 : std::atoi(suffix.c_str());
            if (idx >= 0 && static_cast<std::size_t>(idx) < record.secondaryParts.size())
                return record.secondaryParts[static_cast<std::size_t>(idx)];
            return kInvalidEntityId;
        }
        const auto it = out.roleToEntity.find(role);
        return it != out.roleToEntity.end() ? it->second : kInvalidEntityId;
    };
    const auto nextWeldIdentification = [&]() {
        std::uint64_t seq = doc_.welds().size() + 1;
        std::string digits = std::to_string(seq);
        while (digits.size() < 3) digits = "0" + digits;
        std::string id = "W";
        id += digits;
        return id;
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
                case ComponentObjectKind::Weld:
                    // Sahip olunan kaynak: yeni planda yoksa kaldir (child tablosundaki
                    // entityId = weld id). removeWeld display yapragini da siler ve
                    // shop-weld assembly'lerini yeniden turetir.
                    if (c.entityId == kInvalidEntityId) break;
                    if (const auto wi = doc_.indexOfWeld(c.entityId))
                        doc_.removeWeld(*wi);
                    break;
                default:
                    break;  // construction — sahiplik yok
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
                applyColor(wf, kPlateColor);
                if (pc.record.plate) applyPlateProps(wf, *pc.record.plate);
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
                if (!bolt.targetOwnedPlateRole.empty()) {
                    const auto role = out.roleToEntity.find(bolt.targetOwnedPlateRole);
                    if (role == out.roleToEntity.end() ||
                        std::find(out.ownedPlateIds.begin(), out.ownedPlateIds.end(),
                                  role->second) == out.ownedPlateIds.end()) {
                        rollback();
                        out.error = L"bulonun hedef owned levha rolu bulunamadi: " +
                            std::wstring(bolt.targetOwnedPlateRole.begin(),
                                         bolt.targetOwnedPlateRole.end());
                        return out;
                    }
                    parts.push_back(role->second);
                } else {
                    // Legacy generator: all owned plates remain in the stack.
                    for (const EntityId pid : out.ownedPlateIds)
                        if (pid != kInvalidEntityId) parts.push_back(pid);
                }
                for (const EntityId up : bolt.parts)
                    if (up != kInvalidEntityId &&
                        std::find(parts.begin(), parts.end(), up) == parts.end())
                        parts.push_back(up);
                bolt.parts = std::move(parts);
                resolveHoleShapeParts(bolt, out.ownedPlateIds);  // Tekla Slots in / Hole type

                const double grip =
                    (bolt.gripLength > 0.0) ? bolt.gripLength
                    : (bolt.holeDepth > 0.0) ? bolt.holeDepth
                    : doc_.boltGripLength(bolt);
                const TopoDS_Shape assembly =
                    buildBoltGroupAssemblyShape(bolt, grip > 0.0 ? grip : 15.0);
                WireframeModel display = assembly.IsNull()
                                             ? WireframeModel::cube(10.0)
                                             : shapeToWireframeWithFaces(assembly, 0.15);
                applyColor(display, kBoltColor);

                if (hasStable && doc_.indexOfBoltGroup(stableId)) {
                    bolt.id = stableId;
                    doc_.setBoltGroup(*doc_.indexOfBoltGroup(stableId), bolt);
                    const BoltGroup& updated = doc_.boltGroups()[*doc_.indexOfBoltGroup(stableId)];
                    // GORSEL GEOMETRI GUNCELLE: store_->set yalniz OCC katisini
                    // degistirir; Document'in cizdigi wireframe AYRIDIR. Burada
                    // replaceModel cagrilmazsa ekranda ESKI bulonlar kalir, delikler
                    // ise yeni sayiya gore acilir (sahada goruldu: "onceki bulon
                    // aynen kaliyor, digerlerinin deligi aciliyor").
                    if (auto di = doc_.indexOfEntity(updated.displayEntityId)) {
                        doc_.replaceModel(*di, {std::move(display)});
                    }
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
            case ComponentObjectKind::Weld: {
                const WeldSpec& spec = *pc.record.weld;
                const EntityId partA = resolveWeldPart(spec.partA, spec.partARole);
                const EntityId partB = resolveWeldPart(spec.partB, spec.partBRole);
                if (partA == kInvalidEntityId || partB == kInvalidEntityId || partA == partB) {
                    rollback();
                    out.error = L"Weld parcasi cozulemedi: '" +
                                std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end()) + L"'";
                    return out;
                }
                const auto* shapeA = store_->find(partA);
                const auto* shapeB = store_->find(partB);
                if (!shapeA || !shapeB) {
                    rollback();
                    out.error = L"Weld parcasi B-Rep yok: '" +
                                std::wstring(pc.record.roleKey.begin(), pc.record.roleKey.end()) + L"'";
                    return out;
                }
                Weld w;
                w.mainPart = partA;
                w.secondaryPart = partB;
                w.type = WeldType::Fillet;
                w.size = spec.size > 0.0 ? spec.size : spec.throat;
                w.doubleSided = spec.doubleSided;
                w.weldClass = WeldClass::Shop;
                TopoDS_Shape solid;
                WireframeModel wf;
                const bool geomOk = buildWeldGeometry(w, *shapeA, *shapeB, solid, wf);

                const auto stableWeldIdx = doc_.indexOfWeld(stableId);
                if (hasStable && stableWeldIdx) {
                    const std::size_t wi = *stableWeldIdx;
                    const Weld oldW = doc_.welds()[wi];
                    w.id = stableId;
                    w.identification = oldW.identification;
                    if (geomOk) {
                        if (oldW.displayEntityId != kInvalidEntityId &&
                            doc_.indexOfEntity(oldW.displayEntityId)) {
                            w.displayEntityId = oldW.displayEntityId;
                            doc_.setWeld(wi, w);
                            const Weld& updated = doc_.welds()[wi];
                            if (auto di = doc_.indexOfEntity(updated.displayEntityId)) {
                                doc_.replaceModel(*di, {std::move(wf)});
                                const VersionId before =
                                    store_->activeVersion(updated.displayEntityId);
                                const VersionId after =
                                    store_->set(updated.displayEntityId, solid);
                                if (after != kInvalidVersionId)
                                    doc_.recordGeometryDelta(updated.displayEntityId, before,
                                                             after);
                            }
                        } else {
                            // Eski kaynak glif-only idi — yeni display modeli ekle.
                            doc_.addModel(std::move(wf));
                            const EntityId newDisp = doc_.modelId(doc_.models().size() - 1);
                            w.displayEntityId = newDisp;
                            doc_.setWeld(wi, w);
                            const VersionId before = store_->activeVersion(newDisp);
                            const VersionId after = store_->set(newDisp, solid);
                            if (after != kInvalidVersionId)
                                doc_.recordGeometryDelta(newDisp, before, after);
                        }
                    } else {
                        w.unsupportedReason = w.unsupportedReason.empty()
                                                   ? "kaynak kati uretilemedi"
                                                   : w.unsupportedReason;
                        w.geometrySupported = false;
                        const EntityId oldDisplay = oldW.displayEntityId;
                        w.displayEntityId = kInvalidEntityId;
                        doc_.setWeld(wi, w);
                        if (oldDisplay != kInvalidEntityId)
                            if (auto di = doc_.indexOfEntity(oldDisplay))
                                doc_.removeModelLeaf(*di);
                    }
                    out.roleToEntity[pc.record.roleKey] = stableId;
                } else {
                    w.identification = nextWeldIdentification();
                    if (geomOk) {
                        const std::size_t wi = doc_.addWeld(std::move(w), std::move(wf));
                        const Weld& created = doc_.welds()[wi];
                        if (created.displayEntityId != kInvalidEntityId) {
                            const VersionId before =
                                store_->activeVersion(created.displayEntityId);
                            const VersionId after =
                                store_->set(created.displayEntityId, solid);
                            if (after != kInvalidVersionId)
                                doc_.recordGeometryDelta(created.displayEntityId, before,
                                                         after);
                        }
                        out.roleToEntity[pc.record.roleKey] = created.id;
                    } else {
                        w.unsupportedReason = w.unsupportedReason.empty()
                                                   ? "kaynak kati uretilemedi"
                                                   : w.unsupportedReason;
                        const std::size_t wi = doc_.addWeld(std::move(w));
                        out.roleToEntity[pc.record.roleKey] = doc_.welds()[wi].id;
                    }
                }
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
