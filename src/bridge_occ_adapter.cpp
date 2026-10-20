// MODEL MAKER BRIDGE — OCC-on katman (uygulama).

#include "model_maker/bridge_occ_adapter.hpp"

#ifdef MM_HAS_OCC

#include "model_maker/bolt.hpp"
#include "model_maker/bolt_geometry.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"

#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <sstream>

namespace mm {

namespace {

constexpr double kTol = 1e-6;

double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 scaled(const Vec3& v, double s) {
    return {v.x * s, v.y * s, v.z * s};
}
Vec3 added(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
bool nearZero(double v) { return std::fabs(v) < kTol; }

// BridgeVec3 (paket modeli) -> Vec3 (kernel).
Vec3 toVec3(const BridgeVec3& v) { return {v.x, v.y, v.z}; }

// "x"/"+x"/"-x"/... -> birim eksen. Taninmayan -> false.
bool axisFromString(const std::string& s, Vec3& out) {
    if (s == "x" || s == "+x") { out = {1, 0, 0}; return true; }
    if (s == "-x") { out = {-1, 0, 0}; return true; }
    if (s == "y" || s == "+y") { out = {0, 1, 0}; return true; }
    if (s == "-y") { out = {0, -1, 0}; return true; }
    if (s == "z" || s == "+z") { out = {0, 0, 1}; return true; }
    if (s == "-z") { out = {0, 0, -1}; return true; }
    return false;
}

// "M20" -> 20.0, "20" -> 20.0. Taninamazsa 0.
double parseMetricSize(const std::string& size) {
    std::string digits;
    for (char c : size)
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') digits.push_back(c);
    if (digits.empty()) return 0.0;
    return std::atof(digits.c_str());
}

// Plaka profili mi? ("PL10", "PL10*200", ...)
bool isPlateProfile(const std::string& profile) {
    if (profile.size() < 2) return false;
    char a = static_cast<char>(std::toupper(static_cast<unsigned char>(profile[0])));
    char b = static_cast<char>(std::toupper(static_cast<unsigned char>(profile[1])));
    return a == 'P' && b == 'L';
}

std::string vecStr(const Vec3& v) {
    std::ostringstream os;
    os << "(" << v.x << "," << v.y << "," << v.z << ")";
    return os.str();
}

// Bulon konumlarindan eksen-hizali DUZGUN grid tespiti. Basarili ise out'un
// origin/xDir/countX/countY/spacingX/spacingY alanlari doldurulur ve pattern
// TAM olarak positions'i yeniden uretir. Basarisizsa (scatter/duzensiz/ayni
// duzlemde degil) false — UYDURULMAZ (ayri deliklerle halledilir).
bool detectAxisAlignedBoltGrid(const std::vector<BridgeBolt>& bolts, const Vec3& axis,
                               BoltGroup& out) {
    if (bolts.empty()) return false;
    // eksene dik bir dunya ekseni sec (xDir).
    Vec3 xDir;
    if (std::fabs(axis.x) > 0.5) xDir = {0, 1, 0};
    else if (std::fabs(axis.y) > 0.5) xDir = {1, 0, 0};
    else xDir = {1, 0, 0};
    const Vec3 yDir = cross(axis, xDir);
    if (nearZero(dot(yDir, yDir))) return false;

    const double axisCoord = dot(toVec3(bolts[0].position), axis);
    std::set<double> us, vs;
    for (const auto& b : bolts) {
        if (std::fabs(dot(toVec3(b.position), axis) - axisCoord) > kTol) return false; // ayni duzlemde degil
        us.insert(dot(toVec3(b.position), xDir));
        vs.insert(dot(toVec3(b.position), yDir));
    }
    if (us.size() * vs.size() != bolts.size()) return false; // tam grid degil

    std::vector<double> u(us.begin(), us.end()), v(vs.begin(), vs.end());
    double spacingX = u.size() >= 2 ? u[1] - u[0] : 0.0;
    double spacingY = v.size() >= 2 ? v[1] - v[0] : 0.0;
    for (std::size_t i = 1; i < u.size(); ++i)
        if (std::fabs((u[i] - u[0]) - spacingX * i) > kTol) return false; // duzenli degil
    for (std::size_t i = 1; i < v.size(); ++i)
        if (std::fabs((v[i] - v[0]) - spacingY * i) > kTol) return false;

    // origin = axis*axisCoord + xDir*u0 + yDir*v0
    Vec3 origin = added(added(scaled(axis, axisCoord), scaled(xDir, u[0])), scaled(yDir, v[0]));
    out.origin = origin;
    out.xDir = xDir;
    out.countX = static_cast<int>(u.size());
    out.countY = static_cast<int>(v.size());
    out.spacingX = spacingX;
    out.spacingY = spacingY;

    // Tam dogrulama (toleransli): pattern tum konumlari yeniden uretiyor mu.
    std::vector<Vec3> want;
    for (const auto& b : bolts) want.push_back(toVec3(b.position));
    for (int j = 0; j < out.countY; ++j)
        for (int i = 0; i < out.countX; ++i) {
            const Vec3 p = added(added(out.origin, scaled(out.xDir, out.spacingX * i)),
                                 scaled(yDir, out.spacingY * j));
            bool found = false;
            for (std::size_t k = 0; k < want.size(); ++k) {
                const Vec3 d = added(want[k], scaled(p, -1.0));
                if (nearZero(d.x) && nearZero(d.y) && nearZero(d.z)) { found = true; break; }
            }
            if (!found) return false;
        }
    return true;
}

}  // namespace

TopoDS_Shape buildBridgePlateSolid(const BridgeBBox& bbox) {
    const double dx = bbox.max.x - bbox.min.x;
    const double dy = bbox.max.y - bbox.min.y;
    const double dz = bbox.max.z - bbox.min.z;
    if (dx <= 0.0 || dy <= 0.0 || dz <= 0.0) return TopoDS_Shape{};
    return BRepPrimAPI_MakeBox(gp_Pnt(bbox.min.x, bbox.min.y, bbox.min.z), dx, dy, dz)
        .Shape();
}

BridgeBoltPlan planBridgeBolts(const BridgeBoltGroupInfo& info,
                               const BridgeBBox& plateBBox) {
    BridgeBoltPlan plan;

    // --- Eksen karari ---
    Vec3 axis{1, 0, 0};
    std::string axisSource;
    if (!info.axis.empty()) {
        if (!axisFromString(info.axis, axis)) {
            plan.error = "taninmayan bulon ekseni: '" + info.axis + "'";
            return plan;
        }
        axisSource = info.axisSource.empty() ? "measured" : info.axisSource;
    } else {
        const double dx = plateBBox.max.x - plateBBox.min.x;
        const double dy = plateBBox.max.y - plateBBox.min.y;
        const double dz = plateBBox.max.z - plateBBox.min.z;
        if (dx <= 0 || dy <= 0 || dz <= 0) {
            plan.error = "plaka bbox dejenere; eksen cikarilamadi";
            return plan;
        }
        if (dx <= dy && dx <= dz) axis = {1, 0, 0};
        else if (dy <= dx && dy <= dz) axis = {0, 1, 0};
        else axis = {0, 0, 1};
        axisSource = "inferred";  // en ince boyut = levha kalinligi yonu
    }
    plan.axis = axis;
    plan.axisSource = axisSource;

    // --- Cap karari ---
    double nominal = 0.0;
    for (const auto& b : info.bolts)
        if (b.nominalDiameter > 0.0) { nominal = b.nominalDiameter; break; }
    if (nominal <= 0.0) nominal = parseMetricSize(info.size);

    double holeDiameter = 0.0;
    std::string diameterSource;
    for (const auto& b : info.bolts)
        if (b.holeDiameter > 0.0) {
            holeDiameter = b.holeDiameter;
            diameterSource = b.holeDiameterSource.empty() ? "measured" : b.holeDiameterSource;
            break;
        }
    if (holeDiameter <= 0.0 && nominal > 0.0) {
        holeDiameter = standardBoltHoleDiameter(nominal);  // M20 -> 22 (ISO bosluk)
        diameterSource = "standard";
    }
    if (holeDiameter <= 0.0) {
        plan.error = "delik capi belirlenemedi (size/nominalDiameter yok)";
        return plan;
    }
    plan.holeDiameter = holeDiameter;
    plan.diameterSource = diameterSource;

    // --- Bulonlari dogrula: eksene dik duzlemdeki koordinat plaka ayak izinde mi ---
    const double ax = std::fabs(axis.x), ay = std::fabs(axis.y), az = std::fabs(axis.z);
    for (const auto& b : info.bolts) {
        plan.holeOrigins.push_back(toVec3(b.position));
        bool inside = true;
        if (ax > 0.5) {
            inside = b.position.y >= plateBBox.min.y - kTol && b.position.y <= plateBBox.max.y + kTol &&
                     b.position.z >= plateBBox.min.z - kTol && b.position.z <= plateBBox.max.z + kTol;
        } else if (ay > 0.5) {
            inside = b.position.x >= plateBBox.min.x - kTol && b.position.x <= plateBBox.max.x + kTol &&
                     b.position.z >= plateBBox.min.z - kTol && b.position.z <= plateBBox.max.z + kTol;
        } else {
            inside = b.position.x >= plateBBox.min.x - kTol && b.position.x <= plateBBox.max.x + kTol &&
                     b.position.y >= plateBBox.min.y - kTol && b.position.y <= plateBBox.max.y + kTol;
        }
        plan.boltInside.push_back(inside);
        if (inside) {
            plan.diagnostics.push_back("");
        } else {
            plan.diagnostics.push_back(
                "bulon plaka ayak izi disinda; delik KESILMEDI: " + vecStr(toVec3(b.position)));
        }
    }
    plan.ok = true;
    return plan;
}

BridgeHoleCutResult cutBridgeBoltHoles(const TopoDS_Shape& plateSolid,
                                       const BridgeBoltPlan& plan) {
    BridgeHoleCutResult res;
    if (plateSolid.IsNull()) {
        res.error = "plaka kati bos";
        return res;
    }
    if (!plan.ok || plan.holeDiameter <= 0.0) {
        res.error = plan.error.empty() ? "gecersiz bulon plani" : plan.error;
        return res;
    }
    TopoDS_Shape solid = plateSolid;
    for (std::size_t i = 0; i < plan.holeOrigins.size(); ++i) {
        if (!plan.boltInside[i]) {
            ++res.holesSkipped;
            if (!plan.diagnostics[i].empty()) res.diagnostics.push_back(plan.diagnostics[i]);
            continue;
        }
        const CutModifier mod =
            makeHoleCutModifier(kInvalidEntityId, plan.holeOrigins[i], plan.axis,
                                plan.holeDiameter, 0.0 /* through-cut */);
        const TopoDS_Shape cut = applyCutModifier(solid, mod, Vec3{}, Vec3{}, nullptr);
        if (cut.IsNull()) {
            ++res.holesSkipped;
            res.diagnostics.push_back("delik kesimi basarisiz (boolean null): bulon " +
                                      std::to_string(i) + " " + vecStr(plan.holeOrigins[i]));
            continue;
        }
        solid = cut;
        ++res.holesCut;
    }
    res.solid = solid;
    res.ok = true;
    return res;
}

BridgeImportOutcome importBridgeComponent(Document& doc, const BridgePackage& pkg) {
    BridgeImportOutcome out;

    // --- Kaynak nesneleri ayikla ---
    const BridgeObject* plate = nullptr;
    const BridgeObject* boltObj = nullptr;
    const BridgeObject* weldObj = nullptr;
    for (const auto& o : pkg.objects) {
        if (!plate && (o.kind == "ContourPlate" ||
                       ((o.kind == "Beam") && isPlateProfile(o.profile))))
            plate = &o;
        if (!boltObj && o.kind == "BoltGroup") boltObj = &o;
        if (!weldObj && o.kind == "Weld") weldObj = &o;
    }
    if (!plate || !plate->bbox) {
        out.error = "plaka (ContourPlate + bbox) bulunamadi";
        return out;
    }
    if (!boltObj || !boltObj->boltGroup) {
        out.error = "bulon grubu (BoltGroup + boltGroup) bulunamadi";
        return out;
    }

    // --- Eksen/cap plani ---
    const BridgeBoltPlan plan = planBridgeBolts(*boltObj->boltGroup, *plate->bbox);
    if (!plan.ok) {
        out.error = plan.error;
        return out;
    }

    // --- Plaka kati (deliksiz) ---
    const TopoDS_Shape plateBox = buildBridgePlateSolid(*plate->bbox);
    if (plateBox.IsNull()) {
        out.error = "plaka kati uretilemedi";
        return out;
    }

    // --- Bulon grubu pattern tespiti (uydurulmaz; basarisizsa bireysel delik) ---
    BoltGroup bolt;
    const bool gridOk = detectAxisAlignedBoltGrid(boltObj->boltGroup->bolts, plan.axis, bolt);
    bolt.axis = plan.axis;
    double nominal = 0.0;
    for (const auto& b : boltObj->boltGroup->bolts)
        if (b.nominalDiameter > 0.0) { nominal = b.nominalDiameter; break; }
    if (nominal <= 0.0) nominal = parseMetricSize(boltObj->boltGroup->size);
    bolt.params.nominalDiameter = nominal > 0.0 ? nominal : 20.0;
    bolt.params.holeDiameter = plan.holeDiameter;
    if (!gridOk) {
        // Duzensiz bulon seti: pattern yerine TEKIL delikler kesilir (owner = bulon).
        // countX=countY=1, origin=ilk bulon — display bir bulonu gosterir; geri kalan
        // delikler asagida bireysel addCutModifier ile eklenir.
        bolt.origin = boltObj->boltGroup->bolts.empty()
                          ? Vec3{}
                          : toVec3(boltObj->boltGroup->bolts[0].position);
        bolt.xDir = (std::fabs(plan.axis.x) > 0.5) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        bolt.countX = 1;
        bolt.countY = 1;
        bolt.spacingX = 0.0;
        bolt.spacingY = 0.0;
        out.warnings.push_back(
            "bulon konumlari duzgun eksen-hizali grid degil; bireysel delik yolu kullanildi");
    }

    // --- Atomik transaction ---
    doc.pushSnapshot();
    const auto rollback = [&]() { doc.rollbackOpenRecord(); };

    // --- Plaka varligi ---
    WireframeModel plateWf = shapeToWireframeWithFaces(plateBox, 0.15);
    {
        EntityProperties p = plateWf.properties();
        p.profileName = plate->profile.empty() ? "PL" : plate->profile;
        p.material = plate->material;
        plateWf.setProperties(std::move(p));
    }
    doc.addModel(std::move(plateWf));
    const EntityId plateId = doc.modelId(doc.models().size() - 1);
    OccSolidStore* store = asSolidStore(&doc.occStore());
    if (!store) {
        rollback();
        out.error = "OCC magaza yok";
        return out;
    }
    {
        const VersionId before = store->activeVersion(plateId);
        const VersionId after = store->set(plateId, plateBox);
        if (after == kInvalidVersionId) {
            rollback();
            out.error = "plaka kati yazilamadi";
            return out;
        }
        doc.recordGeometryDelta(plateId, before, after);
        const VersionId oldBase = store->baseVersion(plateId);
        store->setBaseVersion(plateId, after);  // taban = deliksiz kati
        doc.recordBaseVersionDelta(plateId, oldBase, after);
    }
    out.plateId = plateId;

    // --- Bulon grubu ---
    bolt.id = kInvalidEntityId;
    bolt.parts = {plateId};  // yalniz yeniden uretilen levha delinir; ikincil uye YOK (referans)
    const double grip =
        (std::fabs(plan.axis.x) > 0.5) ? (plate->bbox->max.x - plate->bbox->min.x)
        : (std::fabs(plan.axis.y) > 0.5) ? (plate->bbox->max.y - plate->bbox->min.y)
                                         : (plate->bbox->max.z - plate->bbox->min.z);
    const TopoDS_Shape assembly = buildBoltGroupAssemblyShape(bolt, grip > 0.0 ? grip : 10.0);
    WireframeModel display =
        assembly.IsNull() ? WireframeModel::cube(10.0) : shapeToWireframeWithFaces(assembly, 0.15);
    const std::size_t bi = doc.addBoltGroup(bolt, std::move(display));
    const BoltGroup& created = doc.boltGroups()[bi];
    out.boltGroupId = created.id;
    if (!assembly.IsNull()) {
        const VersionId before = store->activeVersion(created.displayEntityId);
        const VersionId after = store->set(created.displayEntityId, assembly);
        if (after != kInvalidVersionId)
            doc.recordGeometryDelta(created.displayEntityId, before, after);
    }

    // Duzensiz set icin bireysel delikler (grid pattern zaten 6 delik uretti ise gerek yok).
    if (!gridOk) {
        for (std::size_t i = 0; i < plan.holeOrigins.size(); ++i) {
            if (!plan.boltInside[i]) {
                ++out.holesSkipped;
                if (!plan.diagnostics[i].empty()) out.diagnostics.push_back(plan.diagnostics[i]);
                continue;
            }
            CutModifier mod =
                makeHoleCutModifier(plateId, plan.holeOrigins[i], plan.axis, plan.holeDiameter, 0.0);
            mod.owner = created.id;
            doc.addCutModifier(std::move(mod));
        }
    }

    // --- Plakaya delikleri uygula (materializer applyCutsFor ile AYNI yol) ---
    {
        VersionId base = store->baseVersion(plateId);
        if (base == kInvalidVersionId) base = store->activeVersion(plateId);
        const TopoDS_Shape* baseShape = base == kInvalidVersionId ? nullptr : store->findVersion(plateId, base);
        if (!baseShape || baseShape->IsNull()) {
            rollback();
            out.error = "plaka taban kati yok (delik uygulanamadi)";
            return out;
        }
        std::vector<CutModifier> mods;
        for (const std::size_t ci : doc.cutModifiersForTarget(plateId))
            mods.push_back(resolveCutModifierSource(doc.cutModifiers()[ci], doc));
        const ToolShapeResolver resolveTool = [&](EntityId id) -> const TopoDS_Shape* {
            return store->find(id);
        };
        const TopoDS_Shape holed =
            applyCutModifierChain(*baseShape, mods, Vec3{}, Vec3{}, resolveTool);
        if (holed.IsNull()) {
            rollback();
            out.error = "delik boolean basarisiz (plaka kati null)";
            return out;
        }
        const auto idxOpt = doc.indexOfEntity(plateId);
        if (!idxOpt) {
            rollback();
            out.error = "plaka varligi bulunamadi";
            return out;
        }
        WireframeModel cutModel = shapeToWireframeWithFaces(holed, 0.15);
        cutModel.setProperties(doc.models()[*idxOpt].properties());
        doc.replaceModel(*idxOpt, {std::move(cutModel)});
        const VersionId before = store->activeVersion(plateId);
        const VersionId after = store->set(plateId, holed);
        if (after == kInvalidVersionId) {
            rollback();
            out.error = "delikli plaka kati yazilamadi";
            return out;
        }
        doc.recordGeometryDelta(plateId, before, after);
    }

    // --- Delik sayimi ---
    out.holesCut = 0;
    for (const auto& m : doc.cutModifiers())
        if (m.kind == CutKind::Hole && m.owner == created.id && m.target == plateId) ++out.holesCut;

    // --- Component instance (orphan = parametrik DEGIL) ---
    ComponentInstanceRecord rec;
    rec.typeId = L"tekla_bridge_import";
    rec.schemaVersion = 2;
    rec.mainPart = kInvalidEntityId;  // kullanici parcalari YENIDEN uretilmez (yalniz referans)
    rec.secondaryParts.clear();
    rec.origin = toVec3(plate->bbox->min);
    rec.ownedPlateIds = {plateId};
    rec.ownedBoltGroupIds = {created.id};
    // Kaynak metadatasi: `_bridge_*` string parametreleri (provenance + refs + weld).
    const auto pushStr = [&](const std::wstring& k, const std::string& v) {
        rec.params.push_back(CompParam{k, CompParamValue{std::wstring(v.begin(), v.end())}});
    };
    pushStr(L"_bridge_formatVersion", pkg.manifest.formatVersion);
    pushStr(L"_bridge_source_guid", pkg.component.guid);
    pushStr(L"_bridge_source_name", pkg.component.name);
    pushStr(L"_bridge_primary", pkg.component.primaryMember.guid + "/" +
                                    pkg.component.primaryMember.profile + "/" +
                                    pkg.component.primaryMember.role);
    for (const auto& sm : pkg.component.secondaryMembers)
        pushStr(L"_bridge_secondary", sm.guid + "/" + sm.profile + "/" + sm.role);
    if (weldObj && weldObj->weld)
        pushStr(L"_bridge_weld", weldObj->weld->type + "/" +
                                     std::to_string(weldObj->weld->size) + "mm/" +
                                     weldObj->weld->mainSourceGUID + "/" +
                                     weldObj->weld->secondarySourceGUID);
    pushStr(L"_bridge_axis_source", plan.axisSource);
    pushStr(L"_bridge_diameter_source", plan.diameterSource);
    // Child tablosu (roleKey -> EntityId; fin plate semantigiyle uyumlu).
    rec.children.push_back(ComponentChildRecord{"shear_tab_near", ComponentObjectKind::Plate,
                                                plateId, {}, std::nullopt, std::nullopt,
                                                std::nullopt, std::nullopt});
    rec.children.push_back(ComponentChildRecord{"beam_bolts", ComponentObjectKind::BoltGroup,
                                                created.id, {}, std::nullopt, std::nullopt,
                                                std::nullopt, std::nullopt});
    const std::size_t ci = doc.addComponentInstance(std::move(rec));
    out.componentId = doc.componentInstances()[ci].id;

    // Uyarilar (davranis sinirlari acikca raporlanir).
    if (weldObj && weldObj->weld)
        out.warnings.push_back("kaynak (weld) yalniz metadata olarak kaydedildi; "
                               "model-maker kaynak geometri motoru yok.");
    out.warnings.push_back("bulon display grip yalniz yeniden uretilen levha; ikincil uye "
                           "(partToBeBolted) yeniden uretilmediginden tam grip YOK.");
    out.warnings.push_back("import edilen component parametrik DEGILDIR (typeId registry'de yok); "
                           "geometri dogrulanmis bbox + bulon konumlarindan YENIDEN uretildi.");

    for (const auto& d : plan.diagnostics)
        if (!d.empty()) out.diagnostics.push_back(d);

    out.ok = true;
    return out;
}

}  // namespace mm

#endif  // MM_HAS_OCC
