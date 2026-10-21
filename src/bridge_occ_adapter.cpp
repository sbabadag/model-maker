// MODEL MAKER BRIDGE — OCC-on katman (uygulama).

#include "model_maker/bridge_occ_adapter.hpp"

#ifdef MM_HAS_OCC

#include "model_maker/bolt.hpp"
#include "model_maker/bolt_geometry.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <ShapeFix_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <gp_Mat.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

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

TopoDS_Shape buildBridgeMeshSolid(const BridgeMesh& mesh) {
    if (mesh.vertices.size() < 4 || mesh.triangles.empty()) return TopoDS_Shape{};

    BRep_Builder B;
    TopoDS_Compound comp;
    B.MakeCompound(comp);

    for (const auto& tri : mesh.triangles) {
        const BridgeVec3& a = mesh.vertices[tri[0]];
        const BridgeVec3& b = mesh.vertices[tri[1]];
        const BridgeVec3& c = mesh.vertices[tri[2]];
        BRepBuilderAPI_MakePolygon wire(gp_Pnt(a.x, a.y, a.z), gp_Pnt(b.x, b.y, b.z),
                                        gp_Pnt(c.x, c.y, c.z), Standard_True);
        if (!wire.IsDone()) continue;
        BRepBuilderAPI_MakeFace face(wire.Wire());
        if (!face.IsDone()) continue;
        B.Add(comp, face.Face());
    }

    // Es-konum kenarlari dik (tolerans mm).
    BRepBuilderAPI_Sewing sew(1e-3);
    sew.Add(comp);
    sew.Perform();
    const TopoDS_Shape sewed = sew.SewedShape();
    if (sewed.IsNull()) return TopoDS_Shape{};

    TopExp_Explorer ex(sewed, TopAbs_SHELL);
    if (!ex.More()) return TopoDS_Shape{};
    const TopoDS_Shell shell = TopoDS::Shell(ex.Current());

    BRepBuilderAPI_MakeSolid maker(shell);
    if (!maker.IsDone()) return TopoDS_Shape{};
    TopoDS_Solid solid = maker.Solid();

    ShapeFix_Shape fix(solid);
    fix.Perform();
    return fix.Shape();
}

TopoDS_Shape transformBridgeShape(const TopoDS_Shape& shape, const BridgeTransform& tr) {
    if (shape.IsNull() || !tr.valid) return shape;
    gp_Trsf t;
    if (tr.hasMatrix) {
        gp_Mat m(tr.matrix[0], tr.matrix[1], tr.matrix[2],
                 tr.matrix[4], tr.matrix[5], tr.matrix[6],
                 tr.matrix[8], tr.matrix[9], tr.matrix[10]);
        const gp_XYZ txyz(tr.matrix[3], tr.matrix[7], tr.matrix[11]);
        t.SetValues(m(1, 1), m(1, 2), m(1, 3), txyz.X(),
                    m(2, 1), m(2, 2), m(2, 3), txyz.Y(),
                    m(3, 1), m(3, 2), m(3, 3), txyz.Z());
    } else {
        const gp_XYZ x(tr.xAxis.x, tr.xAxis.y, tr.xAxis.z);
        const gp_XYZ y(tr.yAxis.x, tr.yAxis.y, tr.yAxis.z);
        const gp_XYZ z(tr.zAxis.x, tr.zAxis.y, tr.zAxis.z);
        const gp_Mat m(x.X(), y.X(), z.X(),
                       x.Y(), y.Y(), z.Y(),
                       x.Z(), y.Z(), z.Z());
        t.SetValues(m(1, 1), m(1, 2), m(1, 3), tr.origin.x,
                    m(2, 1), m(2, 2), m(2, 3), tr.origin.y,
                    m(3, 1), m(3, 2), m(3, 3), tr.origin.z);
    }
    BRepBuilderAPI_Transform xform(shape, t, Standard_True /* copy */);
    if (!xform.IsDone()) return shape;
    return xform.Shape();
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

    // --- Ayni kaynak GUID zaten import edilmis mi? (duplicate import) ---
    const std::wstring srcGuid(pkg.component.guid.begin(), pkg.component.guid.end());
    for (const auto& rec : doc.componentInstances()) {
        if (rec.typeId != L"tekla_bridge_import") continue;
        for (const auto& p : rec.params)
            if (p.name == L"_bridge_source_guid" &&
                std::holds_alternative<std::wstring>(p.value) &&
                std::get<std::wstring>(p.value) == srcGuid) {
                out.ok = true;
                out.duplicate = true;
                out.componentId = rec.id;
                out.warnings.push_back(
                    "ayni kaynak GUID zaten import edilmis; yinelenen import atlandi.");
                return out;
            }
    }

    // --- Parcalari topla (kati tasiyanlar) ---
    struct PartPlan {
        const BridgeObject* obj = nullptr;
        TopoDS_Shape solid;
        bool fromMesh = false;  // mesh -> delikler zaten icinde, tekrar delme
        EntityId id{kInvalidEntityId};
    };
    std::vector<PartPlan> parts;
    for (const auto& o : pkg.objects) {
        const bool solidKind =
            o.kind == "ContourPlate" || o.kind == "Beam" || o.kind == "PolyBeam";
        if (!solidKind || (!o.bbox && !o.mesh)) continue;
        PartPlan pp;
        pp.obj = &o;
        pp.fromMesh = o.mesh.has_value();
        TopoDS_Shape s;
        if (o.mesh && !o.mesh->vertices.empty())
            s = buildBridgeMeshSolid(*o.mesh);
        if (s.IsNull() && o.bbox)
            s = buildBridgePlateSolid(*o.bbox);
        if (s.IsNull()) {
            out.warnings.push_back("parca kati uretilemedi (guid=" + o.sourceGUID +
                                   ", roleKey=" + o.roleKey + "); atlandi.");
            continue;
        }
        if (o.localToGlobal) s = transformBridgeShape(s, *o.localToGlobal);
        pp.solid = s;
        parts.push_back(std::move(pp));
    }
    if (parts.empty()) {
        out.error = "import edilebilir parca (ContourPlate/Beam + bbox/mesh) bulunamadi";
        return out;
    }

    // --- Atomik transaction ---
    doc.pushSnapshot();
    const auto rollback = [&]() { doc.rollbackOpenRecord(); };

    OccSolidStore* store = asSolidStore(&doc.occStore());
    if (!store) {
        rollback();
        out.error = "OCC magaza yok";
        return out;
    }

    // --- Parcalari ekle (kati + wireframe + taban surumu) ---
    for (auto& pp : parts) {
        WireframeModel wf = shapeToWireframeWithFaces(pp.solid, 0.15);
        {
            EntityProperties props = wf.properties();
            props.profileName = pp.obj->profile.empty() ? "PL" : pp.obj->profile;
            props.material = pp.obj->material;
            wf.setProperties(std::move(props));
        }
        doc.addModel(std::move(wf));
        pp.id = doc.modelId(doc.models().size() - 1);
        const VersionId before = store->activeVersion(pp.id);
        const VersionId after = store->set(pp.id, pp.solid);
        if (after == kInvalidVersionId) {
            rollback();
            out.error = "parca kati yazilamadi";
            return out;
        }
        doc.recordGeometryDelta(pp.id, before, after);
        const VersionId oldBase = store->baseVersion(pp.id);
        store->setBaseVersion(pp.id, after);
        doc.recordBaseVersionDelta(pp.id, oldBase, after);
        out.partIds.push_back(pp.id);
    }
    out.plateId = out.partIds.front();
    out.partsImported = static_cast<int>(out.partIds.size());

    const auto findPart = [&](const std::string& guid) -> PartPlan* {
        for (auto& p : parts)
            if (p.obj->sourceGUID == guid) return &p;
        return nullptr;
    };

    // Hedef parcanin TUM delik modifier'larini yeniden uygular (applyCutsFor yolu).
    const auto reapplyHoles = [&](EntityId partId, const char* what) -> bool {
        VersionId base = store->baseVersion(partId);
        if (base == kInvalidVersionId) base = store->activeVersion(partId);
        const TopoDS_Shape* baseShape =
            base == kInvalidVersionId ? nullptr : store->findVersion(partId, base);
        if (!baseShape || baseShape->IsNull()) {
            out.error = std::string(what) + " taban kati yok (delik uygulanamadi)";
            return false;
        }
        std::vector<CutModifier> mods;
        for (const std::size_t ci : doc.cutModifiersForTarget(partId))
            mods.push_back(resolveCutModifierSource(doc.cutModifiers()[ci], doc));
        const ToolShapeResolver resolveTool = [&](EntityId id) -> const TopoDS_Shape* {
            return store->find(id);
        };
        const TopoDS_Shape holed =
            applyCutModifierChain(*baseShape, mods, Vec3{}, Vec3{}, resolveTool);
        if (holed.IsNull()) {
            out.error = std::string(what) + " delik boolean basarisiz";
            return false;
        }
        const auto idxOpt = doc.indexOfEntity(partId);
        if (!idxOpt) {
            out.error = std::string(what) + " varligi bulunamadi";
            return false;
        }
        WireframeModel cutModel = shapeToWireframeWithFaces(holed, 0.15);
        cutModel.setProperties(doc.models()[*idxOpt].properties());
        doc.replaceModel(*idxOpt, {std::move(cutModel)});
        const VersionId before = store->activeVersion(partId);
        const VersionId after = store->set(partId, holed);
        if (after == kInvalidVersionId) {
            out.error = std::string(what) + " delikli kati yazilamadi";
            return false;
        }
        doc.recordGeometryDelta(partId, before, after);
        return true;
    };

    // --- Bulon gruplari ---
    std::vector<std::pair<std::string, EntityId>> boltChildren;
    for (const auto& bo : pkg.objects) {
        if (bo.kind != "BoltGroup" || !bo.boltGroup) continue;

        PartPlan* target = findPart(bo.boltGroup->partToBoltTo);
        if (!target) target = &parts.front();
        if (!target->obj->bbox) {
            out.warnings.push_back("bulon grubu " + bo.sourceGUID +
                                   ": hedef parca bbox yok; delik acilmadi.");
            continue;
        }
        const BridgeBoltPlan plan = planBridgeBolts(*bo.boltGroup, *target->obj->bbox);
        if (!plan.ok) {
            out.warnings.push_back("bulon grubu " + bo.sourceGUID + ": " + plan.error +
                                   " (delik acilmadi).");
            continue;
        }
        const bool skipDrill = target->fromMesh;

        BoltGroup bolt;
        const bool gridOk = detectAxisAlignedBoltGrid(bo.boltGroup->bolts, plan.axis, bolt);
        bolt.axis = plan.axis;
        double nominal = 0.0;
        for (const auto& b : bo.boltGroup->bolts)
            if (b.nominalDiameter > 0.0) { nominal = b.nominalDiameter; break; }
        if (nominal <= 0.0) nominal = parseMetricSize(bo.boltGroup->size);
        bolt.params.nominalDiameter = nominal > 0.0 ? nominal : 20.0;
        bolt.params.holeDiameter = plan.holeDiameter;
        if (!gridOk) {
            bolt.origin = bo.boltGroup->bolts.empty()
                              ? Vec3{}
                              : toVec3(bo.boltGroup->bolts[0].position);
            bolt.xDir = (std::fabs(plan.axis.x) > 0.5) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            bolt.countX = 1;
            bolt.countY = 1;
            bolt.spacingX = 0.0;
            bolt.spacingY = 0.0;
            out.warnings.push_back(
                "bulon konumlari duzgun eksen-hizali grid degil; bireysel delik yolu kullanildi.");
        }

        bolt.id = kInvalidEntityId;
        bolt.parts = skipDrill ? std::vector<EntityId>{} : std::vector<EntityId>{target->id};
        const double grip =
            (std::fabs(plan.axis.x) > 0.5)
                ? (target->obj->bbox->max.x - target->obj->bbox->min.x)
            : (std::fabs(plan.axis.y) > 0.5)
                ? (target->obj->bbox->max.y - target->obj->bbox->min.y)
                : (target->obj->bbox->max.z - target->obj->bbox->min.z);
        const TopoDS_Shape assembly =
            buildBoltGroupAssemblyShape(bolt, grip > 0.0 ? grip : 10.0);
        WireframeModel display =
            assembly.IsNull() ? WireframeModel::cube(10.0)
                              : shapeToWireframeWithFaces(assembly, 0.15);
        const std::size_t bi = doc.addBoltGroup(bolt, std::move(display));
        const BoltGroup& created = doc.boltGroups()[bi];
        out.boltGroupIds.push_back(created.id);
        if (out.boltGroupId == kInvalidEntityId) out.boltGroupId = created.id;
        boltChildren.emplace_back(bo.roleKey.empty() ? "bolt_group" : bo.roleKey,
                                  created.id);
        if (!assembly.IsNull()) {
            const VersionId before = store->activeVersion(created.displayEntityId);
            const VersionId after = store->set(created.displayEntityId, assembly);
            if (after != kInvalidVersionId)
                doc.recordGeometryDelta(created.displayEntityId, before, after);
        }

        if (skipDrill) {
            out.warnings.push_back("bulon grubu " + bo.sourceGUID +
                                   ": hedef parca mesh (delikler kaynak katida zaten var); "
                                   "tekrar delme atlandi (bulonlar gosteriliyor).");
            ++out.boltGroupsImported;
            continue;
        }

        if (!gridOk) {
            for (std::size_t i = 0; i < plan.holeOrigins.size(); ++i) {
                if (!plan.boltInside[i]) {
                    ++out.holesSkipped;
                    if (!plan.diagnostics[i].empty())
                        out.diagnostics.push_back(plan.diagnostics[i]);
                    continue;
                }
                CutModifier mod = makeHoleCutModifier(target->id, plan.holeOrigins[i],
                                                      plan.axis, plan.holeDiameter, 0.0);
                mod.owner = created.id;
                doc.addCutModifier(std::move(mod));
            }
        }
        if (!reapplyHoles(target->id, "parca")) {
            rollback();
            return out;
        }
        for (const auto& d : plan.diagnostics)
            if (!d.empty()) out.diagnostics.push_back(d);
        ++out.boltGroupsImported;
    }

    // --- Delik sayimi ---
    out.holesCut = 0;
    for (const auto& m : doc.cutModifiers())
        for (EntityId bgid : out.boltGroupIds)
            if (m.kind == CutKind::Hole && m.owner == bgid) ++out.holesCut;

    // --- Component instance (orphan = parametrik DEGIL) ---
    ComponentInstanceRecord rec;
    rec.typeId = L"tekla_bridge_import";
    rec.schemaVersion = 3;
    rec.mainPart = kInvalidEntityId;  // kullanici parcalari YENIDEN uretilmez (referans)
    rec.secondaryParts.clear();
    rec.origin = parts.front().obj->bbox ? toVec3(parts.front().obj->bbox->min)
                                         : toVec3(pkg.component.coordinateSystem.origin);
    rec.ownedPlateIds = out.partIds;
    rec.ownedBoltGroupIds = out.boltGroupIds;

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

    // V3: weld / fitting / boolean / relationship metadata (acikca, uydurulmadan).
    int weldSeq = 0;
    for (const auto& o : pkg.objects) {
        if (o.kind == "Weld" && o.weld) {
            const std::wstring key =
                weldSeq == 0 ? L"_bridge_weld" : L"_bridge_weld_" + std::to_wstring(weldSeq);
            ++weldSeq;
            pushStr(key, o.weld->type + "/" + std::to_string(o.weld->size) + "mm/" +
                             o.weld->mainSourceGUID + "/" + o.weld->secondarySourceGUID);
        } else if (o.kind == "Fitting" && o.fitting)
            pushStr(L"_bridge_fitting_" + std::wstring(o.roleKey.begin(), o.roleKey.end()),
                    o.fitting->targetPartGUID + "/" + o.fitting->keepSide +
                        (o.fitting->inferred ? "/inferred" : ""));
        else if (o.kind == "BooleanPart" && o.booleanPart)
            pushStr(L"_bridge_boolean_" + std::wstring(o.roleKey.begin(), o.roleKey.end()),
                    o.booleanPart->operationType + "/" + o.booleanPart->targetPartGUID +
                        "/" + o.booleanPart->operandGUID);
    }
    int opSeq = 0;
    for (const auto& op : pkg.operations)
        pushStr(L"_bridge_op_" + std::to_wstring(opSeq++),
                op.type + "/" + op.targetPartGUID + "/" + op.operatorGUID +
                    (op.inferred ? "/inferred" : ""));

    // Child tablosu (roleKey -> EntityId; semantik, kararli).
    for (const auto& pp : parts) {
        std::string rk = pp.obj->roleKey.empty() ? "part" : pp.obj->roleKey;
        rec.children.push_back(ComponentChildRecord{rk, ComponentObjectKind::Plate, pp.id,
                                                    {}, std::nullopt, std::nullopt,
                                                    std::nullopt, std::nullopt});
    }
    for (const auto& bc : boltChildren)
        rec.children.push_back(ComponentChildRecord{bc.first, ComponentObjectKind::BoltGroup,
                                                    bc.second, {}, std::nullopt,
                                                    std::nullopt, std::nullopt,
                                                    std::nullopt});
    const std::size_t ci = doc.addComponentInstance(std::move(rec));
    out.componentId = doc.componentInstances()[ci].id;

    // Uyarilar (davranis sinirlari acikca raporlanir).
    for (const auto& o : pkg.objects)
        if (o.kind == "Weld")
            out.warnings.push_back("kaynak (weld) yalniz metadata olarak kaydedildi; " +
                                   std::string(o.roleKey) +
                                   " icin model-maker kaynak geometri motoru yok.");
    out.warnings.push_back("bulon display grip yalniz yeniden uretilen levhalar; ikincil "
                           "uye govdeleri yeniden uretilmediginden tam grip YOK.");
    out.warnings.push_back("import edilen component parametrik DEGILDIR (typeId registry'de "
                           "yok); geometri dogrulanmis bbox/mesh + bulon konumlarindan "
                           "YENIDEN uretildi.");

    out.ok = true;
    return out;
}

}  // namespace mm

#endif  // MM_HAS_OCC
