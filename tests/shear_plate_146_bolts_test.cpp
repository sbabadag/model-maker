// TEKLA 146 BOLTS SEKMESI — gercek islev testi (OCC'siz, Linux'ta kosar).
//
// Bolts sekmesindeki her alanin uretilen BoltGroup/PlateSpec uzerinde GERCEK bir
// etkisi oldugunu dogrular (gri/yok sayilan alan yok):
//   tolerance, boltDiameter->delik, holeType (Oversized/Slotted/Tapped/No hole),
//   slotV/slotH/rotateSlots/slotsIn, boltStandard/threadInMat/Site-Workshop/
//   boltingDirection/comment/boltLengthIncrease/cutLength, boltHorizontalPos +
//   boltHorizRef, boltVertRef (fail-closed), Staggering tip 1-4 + Staggered
//   orientation, Sloped (fail-closed).

#include "model_maker/bolt.hpp"
#include "model_maker/component_dialog.hpp"
#include "model_maker/component_engine.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }
static bool sameV(const Vec3& a, const Vec3& b) {
    return approx(a.x, b.x) && approx(a.y, b.y) && approx(a.z, b.z);
}

// kolon dusey 300x300, kiris +Y 200x300 (govde 8 mm), yuz = (0,150,3000).
static ComponentInput makeInput(double webT = 8.0) {
    ComponentInput in;
    in.main.id = EntityId{20};
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = {0, 0, 1};
    in.main.secWidth = 300.0;
    in.main.secHeight = 300.0;
    MemberGeometry beam;
    beam.id = EntityId{21};
    beam.axisFrom = {0, 0, 3000};
    beam.axisTo = {0, 4000, 3000};
    beam.refNormal = {0, 0, 1};
    beam.secWidth = 200.0;
    beam.secHeight = 300.0;
    beam.secWebThickness = webT;
    beam.secFlangeThickness = 12.0;
    in.secondary.push_back(beam);
    return in;
}

static GenerationPlan gen(const std::vector<CompParam>& params, double webT = 8.0) {
    ComponentRegistry reg;
    registerBuiltInComponents(reg);
    ComponentInput in = makeInput(webT);
    in.params = params;
    return reg.generate(L"tekla_shear_plate_146", in);
}
static CompParam S(const wchar_t* n, const wchar_t* v) {
    return CompParam{n, CompParamValue{std::wstring(v)}};
}
static CompParam D(const wchar_t* n, double v) { return CompParam{n, CompParamValue{v}}; }
static CompParam I(const wchar_t* n, int v) { return CompParam{n, CompParamValue{v}}; }
static CompParam B(const wchar_t* n, bool v) { return CompParam{n, CompParamValue{v}}; }

int main() {
    // --- 1) Varsayilan: eski davranis korunur ------------------------------------
    {
        GenerationPlan p = gen({});
        check(p.ok && p.boltGroups.size() == 1, "varsayilan uretildi");
        const BoltGroup& b = p.boltGroups[0];
        check(approx(b.params.holeDiameter, 22.0), "M20 -> delik 22 (katalog)");
        check(b.createHoles && b.holeShapeTarget == 0 && b.staggerMode == 0,
              "varsayilan: yuvarlak delik, stagger yok");
        check(b.params.direction == 1 && b.params.siteWorkshop == 0 &&
                  approx(b.params.lengthIncrease, 0.0) && b.params.standard.empty(),
              "varsayilan bolt properties");
        check(b.parts.size() == 1 && b.parts[0] == EntityId{21}, "varsayilan: kiris delinir");
        // grip = ecc + t/2 + w/2 = 10 + 5 + 4 = 19 ; merkez kaymasi (hi+lo)/2 = 5.5 (-X)
        check(approx(b.gripLength, 19.0), "grip = levha + govde (19)");
        check(approx(b.origin.x, -5.5), "bulon yigin merkezinde (x=-5.5)");
        check(approx(b.origin.y, 210.0), "ortali: yuz + width/2 = 210");
    }

    // --- 2) Delik capi: bulon capi + tolerance ----------------------------------
    {
        GenerationPlan p24 = gen({D(L"boltDiameter", 24.0)});
        check(p24.ok && approx(p24.boltGroups[0].params.holeDiameter, 26.0),
              "M24 -> delik 26 (eski sabit 22 degil)");
        GenerationPlan pt = gen({D(L"tolerance", 3.0)});
        check(pt.ok && approx(pt.boltGroups[0].params.holeDiameter, 23.0),
              "tolerance 3 -> delik 23");
        GenerationPlan ph = gen({D(L"holeDiameter", 25.0)});
        check(ph.ok && approx(ph.boltGroups[0].params.holeDiameter, 25.0),
              "holeDiameter acik verilirse o kullanilir");
        GenerationPlan pk = gen({D(L"holeDiameter", 18.0)});
        check(pk.ok && approx(pk.boltGroups[0].params.holeDiameter, 22.0),
              "delik bulondan kucuk olamaz -> katalog");
    }

    // --- 3) Hole type ------------------------------------------------------------
    {
        GenerationPlan pn = gen({S(L"holeType", L"No hole")});
        check(pn.ok && !pn.boltGroups[0].createHoles, "No hole: createHoles=false");
        check(boltGroupHoleModifiers(pn.boltGroups[0]).empty(), "No hole: delik modifier'i yok");

        GenerationPlan po = gen({S(L"holeType", L"Oversized"), D(L"slotH", 4.0)});
        check(po.ok, "Oversized uretildi");
        BoltGroup bo = po.boltGroups[0];
        check(bo.holeShapeTarget == 1 && approx(bo.shapeHoleDiameter, 26.0), "Oversized: levha 22+4");
        bo.parts = {EntityId{500}, EntityId{21}};  // materializer: {levha, kiris}
        resolveHoleShapeParts(bo, {EntityId{500}});
        const auto mo = boltGroupHoleModifiers(bo);
        check(mo.size() == 8 && approx(mo[0].diameter, 26.0) && approx(mo[1].diameter, 22.0),
              "Oversized: levhada 26, kirista 22");

        // Slotted: slotV=10 -> dusey yuva, yalniz levhada.
        GenerationPlan ps = gen({S(L"holeType", L"Slotted"), D(L"slotV", 10.0)});
        check(ps.ok, "Slotted uretildi");
        BoltGroup bs = ps.boltGroups[0];
        check(approx(bs.slotAlong, 10.0) && approx(bs.slotAcross, 0.0) &&
                  sameV(bs.slotDir, Vec3{0, 0, 1}),
              "Slotted: slotV=10 dusey (sec.height)");
        bs.parts = {EntityId{500}, EntityId{21}};
        resolveHoleShapeParts(bs, {EntityId{500}});
        const auto ms = boltGroupHoleModifiers(bs);
        check(ms.size() == 8 && approx(ms[0].slotAlong, 10.0) && approx(ms[1].slotAlong, 0.0),
              "Slotted: yuva yalniz levha deliginde");

        GenerationPlan pr = gen({S(L"holeType", L"Slotted"), D(L"slotV", 10.0), S(L"rotateSlots", L"Yes")});
        check(pr.ok && approx(pr.boltGroups[0].slotAlong, 0.0) &&
                  approx(pr.boltGroups[0].slotAcross, 10.0),
              "Rotate slots: dusey -> yatay");

        GenerationPlan pb = gen({S(L"holeType", L"Slotted"), D(L"slotH", 8.0), S(L"slotsIn", L"Beam")});
        BoltGroup bb = pb.boltGroups[0];
        bb.parts = {EntityId{500}, EntityId{21}};
        resolveHoleShapeParts(bb, {EntityId{500}});
        const auto mb = boltGroupHoleModifiers(bb);
        check(pb.ok && mb.size() == 8 && approx(mb[0].slotAcross, 0.0) && approx(mb[1].slotAcross, 8.0),
              "Slots in Beam: yuva yalniz kiris govdesinde");

        GenerationPlan pz = gen({S(L"holeType", L"Slotted")});  // slot olcusu yok
        check(pz.ok && pz.boltGroups[0].holeShapeTarget == 0, "Slotted olcusuz = yuvarlak");

        GenerationPlan pp = gen({S(L"holeType", L"Tapped")});
        check(pp.ok && pp.boltGroups[0].holeShapeTarget == 1 &&
                  approx(pp.boltGroups[0].shapeHoleDiameter, 20.0),
              "Tapped: nominal cap (20)");
    }

    // --- 4) Bolt basic properties / yon / artis / aciklama -----------------------
    {
        GenerationPlan p = gen({S(L"boltStandard", L"ISO 4014"), B(L"threadInMat", false),
                                S(L"boltSiteWorkshop", L"Workshop"),
                                S(L"boltingDirection", L"Bolting direction 2"),
                                D(L"boltLengthIncrease", 6.0), S(L"comment", L"deneme")});
        check(p.ok, "bolt properties uretildi");
        const BoltParams& bp = p.boltGroups[0].params;
        check(bp.standard == "ISO 4014" && !bp.threadInMat && bp.siteWorkshop == 1 &&
                  bp.direction == 2 && approx(bp.lengthIncrease, 6.0) && bp.comment == "deneme",
              "standard/thread/site/direction/artis/comment BoltParams'a gecti");
    }

    // --- 5) Cut length -----------------------------------------------------------
    {
        // spans: kiris [-4,4], near levha [5,15]; yon 1: bas +W ucu (15).
        GenerationPlan p5 = gen({D(L"cutLength", 5.0)});
        check(p5.ok && p5.boltGroups[0].parts.empty(), "cutLength 5: kiris govdesi kapsam disi");
        check(approx(p5.boltGroups[0].gripLength, 10.0), "cutLength 5: grip yalniz levha (10)");
        GenerationPlan p100 = gen({D(L"cutLength", 100.0)});
        check(p100.ok && p100.boltGroups[0].parts.size() == 1 &&
                  approx(p100.boltGroups[0].gripLength, 19.0),
              "cutLength 100: tum yigin");
        GenerationPlan pe = gen({D(L"cutLength", 5.0), S(L"boltingDirection", L"Bolting direction 2")});
        check(!pe.ok, "yon 2 + kisa cutLength: levha kapsam disi -> acik hata");
    }

    // --- 6) Yatay konum + referans ----------------------------------------------
    {
        GenerationPlan p = gen({D(L"boltHorizontalPos", 30.0)});
        check(p.ok && approx(p.boltGroups[0].origin.y, 150.0 + 20.0 + 30.0),
              "yatay konum: kiris ucu (yuz+gap 20) + 30 = 200");
        GenerationPlan pm = gen({D(L"boltHorizontalPos", 30.0), S(L"boltHorizRef", L"Main part web")});
        check(pm.ok && approx(pm.boltGroups[0].origin.y, 180.0), "ana parca govdesinden 30 -> 180");
        // 3 kolon x 50 aralik, edgeX 30, konum 40: gereken genislik 20+40+100+30 = 190.
        GenerationPlan pg = gen({D(L"boltHorizontalPos", 40.0), I(L"boltCountX", 3), D(L"pitchX", 50.0),
                                 D(L"edgeX", 30.0)});
        check(pg.ok && approx(pg.plates[0].width, 190.0), "bulon grubu sigmazsa levha 190'a buyur");
        GenerationPlan pc = gen({D(L"boltHorizontalPos", 30.0), S(L"beamEndCut", L"none")});
        check(!pc.ok, "yatay konum + beam end cut none -> acik hata");
        GenerationPlan pv = gen({S(L"boltVertRef", L"Main part")});
        check(!pv.ok, "boltVertRef Main part -> acik hata (sessiz yok sayma yok)");
        GenerationPlan pv2 = gen({S(L"boltVertRef", L"Secondary part")});
        check(pv2.ok, "boltVertRef Secondary part serbest");
    }

    // --- 7) Staggering -----------------------------------------------------------
    {
        const auto count = [&](std::vector<CompParam> pr) {
            pr.push_back(I(L"boltCountX", 3));
            pr.push_back(D(L"pitchX", 50.0));
            pr.push_back(I(L"boltCount", 3));
            GenerationPlan p = gen(pr);
            return std::pair<bool, std::size_t>{p.ok, p.ok ? boltGroupBoltOrigins(p.boltGroups[0]).size() : 0};
        };
        check(count({}).second == 9, "stagger yok: 3x3 = 9");
        check(count({S(L"boltStagger", L"Staggered type 1")}).second == 7, "tip 1: 2-3-2 = 7");
        check(count({S(L"boltStagger", L"Staggered type 2")}).second == 8, "tip 2: 3-2-3 = 8");
        check(count({S(L"boltStaggerExtra", L"Staggered type 3")}).second == 9, "tip 3: 9 (zikzak)");
        check(count({S(L"boltGroupOrientation", L"Staggered")}).second == 9, "orientation Staggered: 9");

        // tip 3: ust (tek) siralar +25 saga; Y ekseni = lenDir.
        std::vector<CompParam> pr3 = {S(L"boltStaggerExtra", L"Staggered type 3"), I(L"boltCountX", 3),
                                      D(L"pitchX", 50.0), I(L"boltCount", 3)};
        GenerationPlan p3 = gen(pr3);
        check(p3.ok && approx(p3.boltGroups[0].staggerOffset, 25.0), "tip 3 offset = pitchX/2");
        if (p3.ok) {
            const auto o = boltGroupBoltOrigins(p3.boltGroups[0]);
            // j=0 alt sira (rowFromTop=3, tek) -> +25 ; j=1 orta (cift) -> 0 ; j=2 ust (tek) -> +25.
            check(o.size() == 9 && approx(o[0].y - o[3].y, 25.0) && approx(o[6].y - o[3].y, 25.0),
                  "tip 3: ust/alt sira orta sirayi gore +25");
        }
        // Staggered orientation: tek kolon (i=1) +yarim dusey aralik (z).
        GenerationPlan po = gen({S(L"boltGroupOrientation", L"Staggered"), I(L"boltCountX", 2),
                                 D(L"pitchX", 60.0), I(L"boltCount", 3), D(L"pitchY", 80.0)});
        check(po.ok && approx(po.boltGroups[0].staggerOffset, 40.0), "orientation Staggered offset = 40");
        if (po.ok) {
            const auto o = boltGroupBoltOrigins(po.boltGroups[0]);
            check(o.size() == 6 && approx(o[1].z - o[0].z, 40.0), "ikinci kolon 40 mm yukari");
        }
        GenerationPlan psl = gen({S(L"boltGroupOrientation", L"Sloped")});
        check(!psl.ok, "orientation Sloped -> acik hata (egimli kiris yok)");
    }

    // --- 8) Dialog: Bolts sekmesi alanlari artik motor parametresi -----------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentType* t = reg.findType(L"tekla_shear_plate_146");
        const ComponentDialogDefinition d = shearPlate146DialogDefinition();
        const wchar_t* realNow[] = {
            L"boltDiameter", L"boltStandard", L"tolerance", L"threadInMat", L"boltSiteWorkshop",
            L"holeType", L"slotV", L"slotH", L"rotateSlots", L"slotsIn", L"cutLength",
            L"comment", L"boltLengthIncrease", L"boltHorizontalPos", L"boltVerticalPos",
            L"boltEdge", L"boltVertRef", L"boltCount", L"boltSpacing", L"edgeX", L"edgeY",
            L"boltCountX", L"pitchX", L"deletedBolts", L"boltStagger", L"boltStaggerExtra",
            L"boltGroupOrientation", L"boltingDirection", L"boltHorizRef", L"boltVerticalMeasure"};
        bool allEngine = (t != nullptr);
        bool noneGrey = true;
        for (const wchar_t* n : realNow) {
            bool inEngine = false;
            if (t) for (const auto& sp : t->params) if (sp.name == n) inEngine = true;
            bool grey = false;
            for (const auto& sp : d.specs) if (sp.name == n && sp.documentedOnly) grey = true;
            if (!inEngine) { allEngine = false; std::printf("  motor parametresi degil: %ls\n", n); }
            if (grey) { noneGrey = false; std::printf("  hala gri: %ls\n", n); }
        }
        check(allEngine, "Bolts alanlari motor parametresi");
        check(noneGrey, "Bolts alanlarinin hicbiri gri (documentedOnly) degil");
    }

    std::printf("%s (%d failure)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
