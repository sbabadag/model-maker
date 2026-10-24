// TEKLA 146 STIFFENERS sekmesi — GERCEK OCCT hacim testleri.
// Berkitme (karsi govde) analitik hacimle: b*h*t - pah alanlari*t. HEB300 kolon
// (tw 11, tf 19), IPE200 kiris (govde 5.6, flans 8.5), tab 120x240x10 (near).
// Case A: kiris kolon flans yuzune (colRef Y). Case B: govde normaline (colRef X).
// Derleme: ~/.hermes/cache/scratch/occbuild/build.sh <bu dosya> <cikti>

#include "model_maker/component_materializer.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_glyph.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"
#include "model_maker/user_catalog.hpp"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-3) { return std::abs(a - b) < tol; }
static Vec3 gColRef{0.0, 1.0, 0.0};
static SectionAnchor gColAnchor = SectionAnchor::Center;
static double gColOffset = 0.0;
static SteelProfile makeProfile(const char* name, double h, double b, double tw, double tf) {
    SteelProfile p;
    p.name = name;
    p.height = h;
    p.width = b;
    p.plateThickness = tw;
    p.flangeThickness = tf;
    return p;
}

// Yeni belge + kolon + kiris kurar; profilleri ve id'leri dondurur.
struct Setup {
    Document doc;
    OccSolidStore* store{nullptr};
    EntityId colId{kInvalidEntityId};
    EntityId beamId{kInvalidEntityId};
    MemberGeometry colGeom;
    MemberGeometry beamGeom;
    double beamMinYBefore{0.0};
};

static Setup makeSetup() {
    Setup s;
    const SteelProfile heb300 = makeProfile("HEB300", 300.0, 300.0, 11.0, 19.0);
    const SteelProfile ipe200 = makeProfile("IPE200", 200.0, 100.0, 5.6, 8.5);

    const TopoDS_Shape colSolid = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0, gColRef,
                                                     gColAnchor);
    const TopoDS_Shape beamSolid = extrudeProfileSolid(ipe200, {0, 1000, 1500}, {0, 60, 1500}, 0.0, {0, 0, 1});

    s.doc.setOccStore(makeOccSolidStore());
    s.store = asSolidStore(&s.doc.occStore());

    WireframeModel colModel = shapeToWireframeWithFaces(colSolid, 0.15);
    {
        EntityProperties p = colModel.properties();
        p.profileName = "HEB300";
        p.axisFromZ = 0; p.axisToZ = 3000;
        p.sectionNormalX = gColRef.x; p.sectionNormalY = gColRef.y; p.sectionNormalZ = gColRef.z;
        colModel.setProperties(std::move(p));
    }
    s.doc.addModel(std::move(colModel));
    s.colId = s.doc.modelId(0);
    s.store->set(s.colId, colSolid);
    s.store->setBaseVersion(s.colId, s.store->activeVersion(s.colId));

    WireframeModel beamModel = shapeToWireframeWithFaces(beamSolid, 0.15);
    {
        EntityProperties p = beamModel.properties();
        p.profileName = "IPE200";
        p.axisFromY = 1000; p.axisFromZ = 1500;
        p.axisToY = 60; p.axisToZ = 1500;
        p.sectionNormalZ = 1;
        beamModel.setProperties(std::move(p));
    }
    s.doc.addModel(std::move(beamModel));
    s.beamId = s.doc.modelId(1);
    s.store->set(s.beamId, beamSolid);
    s.store->setBaseVersion(s.beamId, s.store->activeVersion(s.beamId));

    // Kiris bbox Y-min (kesim oncesi).
    Bnd_Box bb;
    BRepBndLib::Add(beamSolid, bb);
    double _mnx, _mny, _mnz, _mxx, _mxy, _mxz;
    bb.Get(_mnx, _mny, _mnz, _mxx, _mxy, _mxz);
    s.beamMinYBefore = _mny;

    s.colGeom.id = s.colId;
    s.colGeom.axisFrom = {0, 0, 0};
    s.colGeom.axisTo = {0, 0, 3000};
    s.colGeom.secWidth = 300.0;
    s.colGeom.secHeight = 300.0;
    s.colGeom.refNormal = gColRef;
    s.colGeom.sectionCenterOffset = gColOffset;
    s.colGeom.secWebThickness = 11.0;
    s.colGeom.secFlangeThickness = 19.0;
    // sectionCenterOffset = 0 (merkez ankraj) — kati da MERKEZ ankrajli cizilir
    // (makeSetup icinde SectionAnchor::Center). Boylece tab'in temas noktasi
    // gercek kolon yuzune duser ve kaynak kati uretilir.

    s.beamGeom.id = s.beamId;
    s.beamGeom.axisFrom = {0, 1000, 1500};
    s.beamGeom.axisTo = {0, 60, 1500};
    s.beamGeom.secWidth = 100.0;
    s.beamGeom.secHeight = 200.0;
    s.beamGeom.secWebThickness = 5.6;
    s.beamGeom.secFlangeThickness = 8.5;
    return s;
}

static ComponentInput makeInput(const Setup& s) {
    ComponentInput in;
    in.main = s.colGeom;
    in.secondary = {s.beamGeom};
    return in;
}


struct Out {
    bool ok{false};
    bool hasStiff{false};
    double vol{0.0};
    Vec3 bmin{}, bmax{}, cmin{}, cmax{}, umin{1e9, 1e9, 1e9}, umax{-1e9, -1e9, -1e9};
    int count{0};
    double totVol{0.0};
    std::string name, material, finish, desc;
};

static Out runCase(const std::vector<CompParam>& params) {
    Out out;
    Setup s = makeSetup();
    ComponentInput in = makeInput(s);
    in.params = params;
    const GenerationPlan gp = ComponentRegistry::instance().generate(L"tekla_shear_plate_146", in);
    if (!gp.ok) return out;
    const ComponentPlan plan = componentPlanFromGenerationPlan(gp);
    if (!plan.ok) return out;
    ComponentMaterializer mat(s.doc);
    ComponentMaterializer::Prepared prep = mat.prepare(plan, s.colId, {s.beamId});
    if (!prep.ok) return out;
    ComponentInstanceRecord rec;
    rec.typeId = L"tekla_shear_plate_146";
    rec.schemaVersion = 1;
    rec.mainPart = s.colId;
    rec.secondaryParts = {s.beamId};
    ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
    if (!res.ok) return out;
    out.ok = true;
    const auto it = res.roleToEntity.find("stiffener");
    if (it == res.roleToEntity.end() || it->second == kInvalidEntityId) return out;
    out.hasStiff = true;
    const TopoDS_Shape* sh = s.store->find(it->second);
    if (sh && !sh->IsNull()) {
        out.vol = shapeVolume(*sh);
        Bnd_Box bb;
        BRepBndLib::Add(*sh, bb);
        double a, b, c, d, e, f;
        bb.Get(a, b, c, d, e, f);
        out.bmin = {a, b, c};
        out.bmax = {d, e, f};
    }
    for (const char* key : {"stiffener", "stiffener2", "stiffener3", "stiffener4"}) {
        const auto k = res.roleToEntity.find(key);
        if (k == res.roleToEntity.end() || k->second == kInvalidEntityId) continue;
        const TopoDS_Shape* ks = s.store->find(k->second);
        if (!ks || ks->IsNull()) continue;
        ++out.count;
        out.totVol += shapeVolume(*ks);
        Bnd_Box kb;
        BRepBndLib::Add(*ks, kb);
        double a, b, c, d, e, f;
        kb.Get(a, b, c, d, e, f);
        out.umin = {std::min(out.umin.x, a), std::min(out.umin.y, b), std::min(out.umin.z, c)};
        out.umax = {std::max(out.umax.x, d), std::max(out.umax.y, e), std::max(out.umax.z, f)};
    }
    if (const TopoDS_Shape* cs = s.store->find(s.colId)) {
        Bnd_Box cb;
        BRepBndLib::Add(*cs, cb);
        double a, b, c, d, e, f;
        cb.Get(a, b, c, d, e, f);
        out.cmin = {a, b, c};
        out.cmax = {d, e, f};
    }
    if (const auto idx = s.doc.indexOfEntity(it->second)) {
        const auto& p = s.doc.models()[*idx].properties();
        out.name = p.name;
        out.material = p.material;
        out.finish = p.finish;
        out.desc = p.description;
    }
    return out;
}

static CompParam S(const wchar_t* n, const wchar_t* v) {
    return CompParam{n, CompParamValue{std::wstring(v)}};
}
static CompParam D(const wchar_t* n, double v) { return CompParam{n, CompParamValue{v}}; }
static CompParam I(const wchar_t* n, int v) { return CompParam{n, CompParamValue{v}}; }

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    registerBuiltInDialogs(ComponentDialogRegistry::instance());
    const double kPi = 3.14159265358979323846;

    // ---------- Case B (kiris govde normaline gelir; refNormal X) -------------
    gColRef = {1.0, 0.0, 0.0};
    {
        const Out none = runCase({});
        check(none.ok && !none.hasStiff, "B0: Default -> berkitme yok");
        const Out no = runCase({S(L"stiffenerCreation", L"No stiffeners")});
        check(no.ok && !no.hasStiff, "B0: No stiffeners -> yok");

        // Full: b = 150-5.5-10 = 134.5 ; h = beam clear 200-17 = 183 ; t = 10
        const Out full = runCase({S(L"stiffenerCreation", L"Full")});
        std::printf("  B full vol=%.1f bmin=(%.1f %.1f %.1f) bmax=(%.1f %.1f %.1f)\n", full.vol,
                    full.bmin.x, full.bmin.y, full.bmin.z, full.bmax.x, full.bmax.y, full.bmax.z);
        check(full.hasStiff && approx(full.vol, 134.5 * 183.0 * 10.0, 1.0),
              "B1: Full hacmi = b*h*t (134.5 x 183 x 10)");
        // Govdenin KARSI tarafi: kiris +Y, W = (0,-1,0) -> berkitme y<0 (govde yuzu y=-5.5-10)
        check(full.hasStiff && full.bmax.y < -15.0 + 1e-3 && approx(full.bmin.y, -150.0, 0.01),
              "B1: berkitme govdenin KARSI tarafinda (kiris +Y, berkitme y in [-150,-15.5])");
        check(full.hasStiff && approx(full.bmax.y, -15.5, 0.01), "B1: govde bosluguna 10 mm (5.5+10)");

        const Out det = runCase({S(L"stiffenerCreation", L"Determined by shear tab")});
        check(det.hasStiff && approx(det.vol, 134.5 * 240.0 * 10.0, 1.0),
              "B2: Determined by shear tab: yukseklik = tab (240)");
        // Partial: tab (120) beam govdesinden (183) kisa -> ust Full (91.5), alt tab alti (-60).
        const Out part = runCase({S(L"stiffenerCreation", L"Partial"), D(L"plateHeight", 120.0),
                                  I(L"boltCount", 2)});
        check(part.hasStiff && approx(part.vol, 134.5 * (91.5 + 60.0) * 10.0, 1.0),
              "B3: Partial: ust Full (91.5), alt tab alti (-60)");

        const Out gaps = runCase({S(L"stiffenerCreation", L"Full"), D(L"stiffenerGapFlange", 5.0),
                                  D(L"stiffenerGapWeb", 4.0)});
        check(gaps.hasStiff && approx(gaps.vol, (150.0 - 5.5 - 4.0) * (183.0 - 10.0) * 10.0, 1.0),
              "B4: gap1=5 (flans) ve gap2=4 (govde) olculeri");

        const Out dims = runCase({S(L"stiffenerCreation", L"Full"), D(L"stiffenerT", 14.0),
                                  D(L"stiffenerW", 100.0), D(L"stiffenerH", 120.0)});
        check(dims.hasStiff && approx(dims.vol, 100.0 * 120.0 * 14.0, 1.0),
              "B5: t/b/h acik girilirse onlar kullanilir");

        // Pah: 20x20 govde tarafindaki 2 kose
        const double base = 134.5 * 183.0 * 10.0;
        const Out ln = runCase({S(L"stiffenerCreation", L"Full"), S(L"stiffenerChamferType", L"Line chamfer"),
                                D(L"stiffenerChamferH", 20.0), D(L"stiffenerChamferV", 20.0)});
        check(ln.hasStiff && approx(ln.vol, base - 2.0 * 200.0 * 10.0, 1.0), "B6: dogru pah 2 kose");
        const Out cx = runCase({S(L"stiffenerCreation", L"Full"), S(L"stiffenerChamferType", L"Convex arc chamfer"),
                                D(L"stiffenerChamferH", 20.0), D(L"stiffenerChamferV", 20.0)});
        check(cx.hasStiff && approx(cx.vol, base - 2.0 * 400.0 * (1.0 - kPi / 4.0) * 10.0, 1.0),
              "B7: konveks yay pah (a^2(1-pi/4))");
        const Out cc = runCase({S(L"stiffenerCreation", L"Full"), S(L"stiffenerChamferType", L"Concave arc chamfer"),
                                D(L"stiffenerChamferH", 20.0), D(L"stiffenerChamferV", 20.0)});
        check(cc.hasStiff && approx(cc.vol, base - 2.0 * (kPi * 400.0 / 4.0) * 10.0, 1.0),
              "B8: konkav yay pah (pi a^2 / 4)");
        const Out nch = runCase({S(L"stiffenerCreation", L"Full"), S(L"stiffenerChamferType", L"No chamfer"),
                                 D(L"stiffenerChamferH", 20.0), D(L"stiffenerChamferV", 20.0)});
        check(nch.hasStiff && approx(nch.vol, base, 1.0), "B9: No chamfer -> pahsiz");

        // Yatay (XY'ye paralel) sureklilik levhalari: Case B -> yalniz karsi cep, ust+alt
        // baslik kotu = 2 levha, her biri 134.5 x 262 x 10 (kiris baslik orta kotu 1500 +- 95.75)
        const Out hor = runCase({S(L"stiffenerCreation", L"Full"),
                                 S(L"stiffenerOrientation", L"Perpendicular to main part")});
        check(hor.count == 2 && approx(hor.totVol, 2.0 * 134.5 * 262.0 * 10.0, 2.0),
              "B10: Perpendicular Full: 2 levha (ust+alt kot), toplam hacim 2 x 134.5 x 262 x 10");
        check(hor.count == 2 && approx(hor.umin.z, 1500.0 - 95.75 - 5.0, 0.01) &&
                  approx(hor.umax.z, 1500.0 + 95.75 + 5.0, 0.01),
              "B10: levhalar kirisin alt/ust baslik kotunda (z 1399.25..1600.75 araliginda)");

        // Parca ozellikleri
        const Out props = runCase({S(L"stiffenerCreation", L"Full"), S(L"stiffenerMaterial", L"S355"),
                                   S(L"stiffenerName", L"RIB1"), S(L"stiffenerFinish", L"Galvaniz"),
                                   S(L"stiffenerPosPrefix", L"S"), I(L"stiffenerPosStart", 7)});
        check(props.hasStiff && props.material == "S355" && props.name == "RIB1" &&
                  props.finish == "Galvaniz" && props.desc == "Pos_No: S7",
              "B11: material/name/finish/Pos_No parcaya yazilir");
    }

    // ---------- Case A (kiris kolon flans yuzune gelir; refNormal Y) ---------
    gColRef = {0.0, 1.0, 0.0};
    {
        const Out full = runCase({S(L"stiffenerCreation", L"Full")});
        std::printf("  A full vol=%.1f bmin=(%.1f %.1f %.1f) bmax=(%.1f %.1f %.1f)\n", full.vol,
                    full.bmin.x, full.bmin.y, full.bmin.z, full.bmax.x, full.bmax.y, full.bmax.z);
        // govdeye paralel: b = 300-38 = 262 (H), h = 183, t = 10, x = tab'in karsisi (+)
        check(full.hasStiff && approx(full.vol, 262.0 * 183.0 * 10.0, 1.0),
              "A1: Full hacmi = 262 x 183 x 10 (govdeye paralel)");
        check(full.hasStiff && approx(full.bmin.x, 15.5, 0.01) && approx(full.bmax.x, 25.5, 0.01),
              "A1: govdenin karsi yani (x in [15.5, 25.5]; tab x<0 tarafinda)");
        const Out ch = runCase({S(L"stiffenerCreation", L"Full"), S(L"stiffenerChamferType", L"Line chamfer"),
                                D(L"stiffenerChamferH", 20.0), D(L"stiffenerChamferV", 20.0)});
        check(ch.hasStiff && approx(ch.vol, 262.0 * 183.0 * 10.0 - 4.0 * 200.0 * 10.0, 1.0),
              "A2: Case A dort kosede pah");
    }

    // ---------- VARSAYILAN ANKRAJ (Top): kolon kesiti eksenden kaymis -------------
    // Berkitme HER ZAMAN kolon kati icinde (bbox) durmali; eksen kesit ustunden gecse de.
    gColAnchor = SectionAnchor::Top;
    gColOffset = -150.0;
    for (int cs = 0; cs < 2; ++cs) {
        gColRef = cs == 0 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0};  // A, B
        for (const wchar_t* orient : {L"Default", L"Perpendicular to main part"}) {
            const Out o = runCase({S(L"stiffenerCreation", L"Full"), S(L"stiffenerOrientation", orient)});
            const double tol = 0.01;
            const bool inside = o.count > 0 && o.umin.x >= o.cmin.x - tol && o.umax.x <= o.cmax.x + tol &&
                                o.umin.y >= o.cmin.y - tol && o.umax.y <= o.cmax.y + tol &&
                                o.umin.z >= o.cmin.z - tol && o.umax.z <= o.cmax.z + tol;
            std::printf("  Top-anchor %s %ls: n=%d stf (%.1f..%.1f, %.1f..%.1f) col (%.1f..%.1f, %.1f..%.1f)\n",
                        cs == 0 ? "A" : "B", orient, o.count, o.umin.x, o.umax.x, o.umin.y, o.umax.y,
                        o.cmin.x, o.cmax.x, o.cmin.y, o.cmax.y);
            if (cs == 1 && std::wstring(orient) == L"Default") {
                // Case B + Top ankraj: kiris ekseni (H=0) kolonun UST kenarinda kalir, dusey
                // levha kesit cebine hic sigmaz -> fail-closed (hicbir berkitme uretilmez),
                // kolonun disina levha ATILMAZ.
                check(o.count == 0, "Top ankraj Case B dusey: cebe sigmiyorsa berkitme uretilmez (fail-closed)");
                continue;
            }
            check(inside, cs == 0 ? "Top ankraj Case A: tum berkitmeler kolon kati icinde"
                                  : "Top ankraj Case B: tum berkitmeler kolon kati icinde");
            if (std::wstring(orient) == L"Perpendicular to main part")
                check(o.count == (cs == 0 ? 4 : 2), cs == 0 ? "Top ankraj Case A dik: 4 levha" : "Top ankraj Case B dik: 2 levha");
        }
    }

    std::printf("%s (%d failure)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
