// ALIN LEVHASI BAGLANTISI (Phase C1) — OCC'siz core test.
//
// EndPlateConnection yerlesim (computeEndPlatePlacement) + Document kaydi +
// alt nesne sahipligi (levha + bulon grubu) + undo/redo + MMW8 round-trip +
// deleteModels kaskadi. windows.h yok; Linux'ta kosar.
//
// Semantik: addEndPlateConnection levha modeli + bulon grubu (delikler +
// montaj) + baglanti kaydi ekler; removeEndPlateConnection levhayi + bulon
// grubunu siler ama KULLANICI parcalarini (main/secondary) KORUR.

#include "model_maker/document.hpp"
#include "model_maker/end_plate.hpp"

#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace mm;

static int g_checks = 0;
static int g_failures = 0;
static void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL %s\n", what);
    }
}

static bool approx(double a, double b, double tol = 1e-6) {
    return std::fabs(a - b) <= tol;
}

int main() {
    // --- 1) Yerlesim hesabi -------------------------------------------------
    {
        EndPlateParams p;
        p.plateThickness = 12.0;
        p.plateExtra = 20.0;
        p.boltCountX = 2;
        p.boltCountY = 2;
        p.boltEdge = 30.0;
        // Kiris +X yonunde, kesit bbox 200x300 (genislik x yukseklik).
        const EndPlatePlacement pl =
            computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, p);
        check(approx(pl.plateWidth, 240.0), "levha genisligi 240");
        check(approx(pl.plateHeight, 340.0), "levha yuksekligi 340");
        check(approx(pl.axis.x, 1.0) && approx(pl.axis.y, 0.0) && approx(pl.axis.z, 0.0),
              "eksen kiris yonu +X");
        // u = Z x X = +Y (genislik), v = X x Y = +Z (yukseklik).
        check(approx(pl.u.y, 1.0) || approx(pl.u.y, -1.0), "u genislik yonu Y ekseninde");
        check(approx(pl.v.z, 1.0) || approx(pl.v.z, -1.0), "v yukseklik yonu Z ekseninde");
        check(pl.bolts.countX == 2 && pl.bolts.countY == 2, "2x2 bulon");
        // Genislik yonu: flans/2 = 200/2 = 100 (Tekla gauge cizgisi). Yukseklik
        // yonu levha boyutundan kalir: 340 - 2*30 = 280.
        check(approx(pl.bolts.spacingX, 100.0), "spacingX 100 (flans/2)");
        check(approx(pl.bolts.spacingY, 280.0), "spacingY 280");
        const std::vector<Vec3> origins = boltGroupBoltOrigins(pl.bolts);
        check(origins.size() == 4, "4 bulon merkezi");
        check(approx(pl.bolts.params.nominalDiameter, 20.0), "M20");
        check(approx(pl.bolts.params.holeDiameter, 22.0), "delik 22");
    }

    // --- 1b) Ayrı ust/alt kenar payi (Tekla 144 Picture tab) ----------------
    {
        EndPlateParams p;
        p.plateThickness = 12.0;
        p.plateExtra = 20.0;   // yatay
        p.topExtra = 40.0;     // ust
        p.bottomExtra = 10.0;  // alt
        const EndPlatePlacement pl =
            computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, p);
        check(approx(pl.plateHeight, 350.0), "asimetrik yukseklik 300+40+10=350");
        check(approx(pl.plateWidth, 240.0), "genislik yatay paydan 240");
        // v = +Z (yukseklik yonu); merkez (top-bottom)/2 = 15 yukari kayar.
        check(approx(pl.plateCenter.z, 15.0), "merkez ust paya dogru 15 kayar");

        // Geriye uyum: top/bottom -1 -> plateExtra kullanilir (simetrik, kaymaz).
        EndPlateParams q;
        q.plateThickness = 12.0;
        q.plateExtra = 20.0;
        const EndPlatePlacement pl2 =
            computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, q);
        check(approx(pl2.plateHeight, 340.0), "varsayilan simetrik 340");
        check(approx(pl2.plateCenter.z, 0.0), "varsayilan merkez kaymaz");
    }

    // --- 1b2) Tekla 144 isaret sozlesmesi: pozitif kenar mesafesi KUCULTUR ---
    {
        EndPlateParams p;
        p.plateThickness = 12.0;
        p.plateExtra = 20.0;
        p.upperEdgeDistance = 10.0;   // Tekla: pozitif = ust kenar iceri (kucult)
        p.lowerEdgeDistance = 20.0;   // pozitif = alt kenar iceri (kucult)
        const EndPlatePlacement pl =
            computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, p);
        check(approx(pl.plateHeight, 270.0), "Tekla: 300-10-20=270 (pozitif kucultur)");
        check(approx(pl.plateCenter.z, 5.0), "Tekla: merkez (20-10)/2=5");

        // Negatif = buyutur.
        EndPlateParams n;
        n.plateThickness = 12.0;
        n.plateExtra = 20.0;
        n.upperEdgeDistance = -10.0;
        n.lowerEdgeDistance = -10.0;
        const EndPlatePlacement plN =
            computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, n);
        check(approx(plN.plateHeight, 320.0), "Tekla: 300+10+10=320 (negatif buyutur)");

        // Migrasyon denkligi: legacy topExtra=+40 == Tekla upperEdgeDistance=-40.
        EndPlateParams legacy;
        legacy.plateThickness = 12.0;
        legacy.plateExtra = 20.0;
        legacy.topExtra = 40.0;
        legacy.bottomExtra = 10.0;
        const EndPlatePlacement plL =
            computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, legacy);
        EndPlateParams tekla;
        tekla.plateThickness = 12.0;
        tekla.plateExtra = 20.0;
        tekla.upperEdgeDistance = -40.0;
        tekla.lowerEdgeDistance = -10.0;
        const EndPlatePlacement plT =
            computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, tekla);
        check(approx(plL.plateHeight, plT.plateHeight), "migrasyon: legacy==Tekla yukseklik");
        check(approx(plL.plateCenter.z, plT.plateCenter.z), "migrasyon: legacy==Tekla merkez");

        // Acik boyut override: otomatik boyut uretimi ezilir.
        EndPlateParams ov;
        ov.plateThickness = 12.0;
        ov.plateExtra = 20.0;
        ov.plateWidthOverride = 500.0;
        ov.plateHeightOverride = 400.0;
        const EndPlatePlacement plO =
            computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, ov);
        check(approx(plO.plateWidth, 500.0), "acik genislik override 500");
        check(approx(plO.plateHeight, 400.0), "acik yukseklik override 400");
        // Genislik override'i bulon gauge'ini bozmamali: gauge kiris kesit
        // genisliginden (200) gelir, levha boyutundan (500) degil -> 100.
        check(approx(plO.bolts.spacingX, 100.0),
              "override: bulon gauge flanstan (100), levha boyutundan degil");
    }

    // --- 1c) Bolt group dimensions (Tekla): hizalama + aralik + silme -------
    {
        EndPlateParams p;
        p.plateThickness = 12.0;
        p.plateExtra = 20.0;
        p.boltSpacingX = 100.0;  // elle aralik
        p.boltSpacingY = 120.0;
        const EndPlatePlacement pl =
            computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, p);
        check(approx(pl.bolts.spacingX, 100.0), "elle yatay aralik 100");
        check(approx(pl.bolts.spacingY, 120.0), "elle dikey aralik 120");

        // Sol+Alt hizalama (offset<0 -> boltEdge=30): en sol-alt bulon levha
        // sol-alt kosesinden 30'ar mm iceride. u=+Y, v=+Z; levha 240x340,
        // merkez (6,0,0). origin = center - u*(120-30) - v*(170-30) = (6,-90,-140).
        EndPlateParams a;
        a.plateThickness = 12.0;
        a.plateExtra = 20.0;
        a.boltAlignH = 0;   // Sol
        a.boltAlignV = 2;   // Alt
        a.boltOffsetH = -1.0;  // -> boltEdge
        a.boltOffsetV = -1.0;  // -> boltEdge
        const EndPlatePlacement plA =
            computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, a);
        check(approx(plA.bolts.origin.y, -90.0), "sol hizalama origin.y -90");
        check(approx(plA.bolts.origin.z, -140.0), "alt hizalama origin.z -140");

        // Silinen bulonlar: 2 bulondan 2 numara silinince 1 bulon kalir.
        BoltGroup g;
        g.countX = 2;
        g.countY = 1;
        g.spacingX = 100.0;
        g.origin = {0.0, 0.0, 0.0};
        g.axis = {0.0, 0.0, 1.0};
        g.xDir = {1.0, 0.0, 0.0};
        g.deletedBolts = {2};
        const std::vector<Vec3> origins = boltGroupBoltOrigins(g);
        check(origins.size() == 1u, "silinen bulon atlandi (2 -> 1)");
        if (origins.size() == 1u) check(approx(origins[0].x, 0.0), "kalan 1. bulon");

        // Degisken bulon araliklari (Tekla "each space between bolts"): countX=3,
        // spacingXs={80,120} -> kumulatif x = 0, 80, 200 (sabit spacingX yok sayilir).
        BoltGroup vg;
        vg.countX = 3;
        vg.countY = 1;
        vg.spacingX = 100.0;  // sabit (kullanilmaz: spacingXs oncelikli)
        vg.spacingXs = {80.0, 120.0};
        vg.origin = {0.0, 0.0, 0.0};
        vg.axis = {0.0, 0.0, 1.0};
        vg.xDir = {1.0, 0.0, 0.0};
        const std::vector<Vec3> vorg = boltGroupBoltOrigins(vg);
        check(vorg.size() == 3u, "degisken aralik: 3 bulon");
        if (vorg.size() == 3u) {
            check(approx(vorg[0].x, 0.0), "degisken: 1. bulon x=0");
            check(approx(vorg[1].x, 80.0), "degisken: 2. bulon x=80");
            check(approx(vorg[2].x, 200.0), "degisken: 3. bulon x=200");
        }
        check(approx(boltGroupSpanX(vg), 200.0), "degisken: grup acikligi 200");
    }

    // --- 2) Document kaydi + sahiplik ---------------------------------------
    Document doc;
    doc.pushSnapshot();
    doc.addModel(WireframeModel::rectangle({0, 0, 0}, {300, 300, 0}));   // kolon kesiti
    const EntityId column = doc.modelIds().back();
    doc.addModel(WireframeModel::rectangle({0, 0, 0}, {100, 100, 0}));   // kiris kesiti
    const EntityId beam = doc.modelIds().back();
    const std::size_t userModelCount = doc.models().size();

    EndPlateParams p;
    p.plateThickness = 12.0;
    p.boltCountX = 2;
    p.boltCountY = 2;
    const EndPlatePlacement pl = computeEndPlatePlacement({0, 0, 0}, {1, 0, 0}, 200.0, 300.0, p);

    EndPlateConnection conn;
    conn.mainPart = column;
    conn.secondaryPart = beam;
    conn.params = p;
    conn.bolts = pl.bolts;

    doc.pushSnapshot();
    const std::size_t ci = doc.addEndPlateConnection(
        conn, WireframeModel::rectangle({0, 0, 0}, {240, 340, 0}),   // levha (placeholder)
        WireframeModel::cube(10.0));                                   // bulon montaji (placeholder)
    check(ci == 0, "baglanti indeksi 0");
    check(doc.endPlateConnections().size() == 1, "1 baglanti kaydi");
    check(doc.endPlateConnections()[0].id != kInvalidEntityId, "baglanti id gecerli");
    check(doc.endPlateConnections()[0].plateEntityId != kInvalidEntityId,
          "levha id atandi");
    check(doc.endPlateConnections()[0].bolts.id != kInvalidEntityId,
          "bulon grubu id atandi");
    check(doc.boltGroups().size() == 1, "1 bulon grubu kaydi");
    // Delikler: 4 bulon x 2 parca (levha + kolon) = 8 Hole modifier.
    std::size_t holeCount = 0;
    for (const auto& m : doc.cutModifiers())
        if (m.kind == CutKind::Hole) ++holeCount;
    check(holeCount == 8, "8 delik (4 bulon x levha+kolon)");

    // Sahiplik: levha modeli + bulon montaji eklendi => +2 model.
    check(doc.models().size() == userModelCount + 2, "levha + montaj modeli eklendi");

    // --- 3) removeEndPlateConnection: kullanici parcalari KORUNUR -----------
    doc.pushSnapshot();
    doc.removeEndPlateConnection(0);
    check(doc.endPlateConnections().empty(), "baglanti kaldirildi");
    check(doc.boltGroups().empty(), "bulon grubu kaldirildi");
    check(doc.models().size() == userModelCount, "levha + montaj silindi, parcalar korundu");
    bool columnStillThere = false, beamStillThere = false;
    for (const EntityId id : doc.modelIds())
        if (id == column) columnStillThere = true;
        else if (id == beam) beamStillThere = true;
    check(columnStillThere && beamStillThere, "kolon + kiris korundu");

    // --- 4) Undo/redo -------------------------------------------------------
    doc.undo();
    check(doc.endPlateConnections().size() == 1, "undo: baglanti geri geldi");
    check(doc.boltGroups().size() == 1, "undo: bulon grubu geri geldi");
    doc.redo();
    check(doc.endPlateConnections().empty(), "redo: baglanti tekrar kaldirildi");

    // --- 5) deleteModels kaskadi: main silinince baglanti kaldirilir --------
    {
        Document d2;
        d2.pushSnapshot();
        d2.addModel(WireframeModel::rectangle({0, 0, 0}, {300, 300, 0}));
        const EntityId col = d2.modelIds().back();
        d2.addModel(WireframeModel::rectangle({0, 0, 0}, {100, 100, 0}));
        const EntityId bm = d2.modelIds().back();
        EndPlateConnection c2;
        c2.mainPart = col;
        c2.secondaryPart = bm;
        c2.params = p;
        c2.bolts = pl.bolts;
        d2.pushSnapshot();
        d2.addEndPlateConnection(c2, WireframeModel::rectangle({0, 0, 0}, {240, 340, 0}),
                                 WireframeModel::cube(10.0));
        check(d2.endPlateConnections().size() == 1, "kaskad oncesi 1 baglanti");
        d2.pushSnapshot();
        const auto colIdx = d2.indexOfEntity(col);
        d2.deleteModels({*colIdx});
        check(d2.endPlateConnections().empty(), "main silinince baglanti kaldirildi");
        check(d2.boltGroups().empty(), "main silinince bulon grubu kaldirildi");
        // Kiris (secondary) KORUNMALI — deleteModels yalniz kolonu sildi.
        bool beamKept = false;
        for (const EntityId id : d2.modelIds())
            if (id == bm) beamKept = true;
        check(beamKept, "secondary (kiris) korundu");
    }

    // --- 6) MMW8 round-trip -------------------------------------------------
    {
        Document d3;
        d3.pushSnapshot();
        d3.addModel(WireframeModel::rectangle({0, 0, 0}, {300, 300, 0}));
        const EntityId col = d3.modelIds().back();
        d3.addModel(WireframeModel::rectangle({0, 0, 0}, {100, 100, 0}));
        const EntityId bm = d3.modelIds().back();
        EndPlateConnection c3;
        c3.mainPart = col;
        c3.secondaryPart = bm;
        c3.params = p;
        c3.params.gap = 5.0;  // gap serilestirmesi dogrulaniyor
        c3.params.boltAlignH = 2;   // Sag
        c3.params.boltAlignV = 0;   // Ust
        c3.params.boltOffsetH = 40.0;
        c3.params.boltOffsetV = 35.0;
        c3.params.boltSpacingX = 110.0;
        c3.params.boltSpacingY = 130.0;
        c3.params.boltSpacingXs = {110.0};
        c3.params.boltSpacingYs = {130.0};
        c3.params.deletedBolts = {2, 4};
        c3.params.boltStandard = "ISO 4014";
        c3.params.threadInMat = true;
        c3.params.siteWorkshop = 1;
        c3.params.boltCutLengthMode = 1;
        c3.params.boltLengthIncrease = 15.0;
        c3.params.boltDirection = 2;
        c3.params.boltComment = "test yorum";
        c3.params.upperEdgeDistance = 15.0;
        c3.params.lowerEdgeDistance = -5.0;
        c3.params.plateWidthOverride = 450.0;
        c3.params.plateHeightOverride = 320.0;
        c3.params.plateMaterial = "S235JR";
        c3.bolts = pl.bolts;
        d3.pushSnapshot();
        d3.addEndPlateConnection(c3, WireframeModel::rectangle({0, 0, 0}, {240, 340, 0}),
                                 WireframeModel::cube(10.0));
        const EntityId connId = d3.endPlateConnections()[0].id;
        const EntityId boltId = d3.endPlateConnections()[0].bolts.id;
        const EntityId plateId = d3.endPlateConnections()[0].plateEntityId;

        const auto path = std::filesystem::temp_directory_path() / "mm_end_plate_test.mmw8";
        d3.save(path);

        Document d4;
        d4.load(path);
        std::filesystem::remove(path);
        check(d4.endPlateConnections().size() == 1, "round-trip: 1 baglanti");
        check(d4.endPlateConnections()[0].id == connId, "round-trip: baglanti id korundu");
        check(d4.endPlateConnections()[0].mainPart == col, "round-trip: mainPart korundu");
        check(d4.endPlateConnections()[0].secondaryPart == bm, "round-trip: secondary korundu");
        check(d4.endPlateConnections()[0].bolts.id == boltId, "round-trip: bulon id korundu");
        check(d4.endPlateConnections()[0].plateEntityId == plateId, "round-trip: levha id korundu");
        check(d4.endPlateConnections()[0].bolts.countX == 2, "round-trip: countX 2");
        check(approx(d4.endPlateConnections()[0].params.plateThickness, 12.0),
              "round-trip: kalinlik 12");
        check(approx(d4.endPlateConnections()[0].params.gap, 5.0),
              "round-trip: gap 5.0 serilestirildi");
        check(d4.endPlateConnections()[0].params.boltAlignH == 2, "round-trip: alignH Sag");
        check(d4.endPlateConnections()[0].params.boltAlignV == 0, "round-trip: alignV Ust");
        check(approx(d4.endPlateConnections()[0].params.boltOffsetH, 40.0),
              "round-trip: offH 40");
        check(approx(d4.endPlateConnections()[0].params.boltOffsetV, 35.0),
              "round-trip: offV 35");
        check(approx(d4.endPlateConnections()[0].params.boltSpacingX, 110.0),
              "round-trip: spacingX 110");
        check(approx(d4.endPlateConnections()[0].params.boltSpacingY, 130.0),
              "round-trip: spacingY 130");
        check(d4.endPlateConnections()[0].params.deletedBolts.size() == 2 &&
                  d4.endPlateConnections()[0].params.deletedBolts[0] == 2 &&
                  d4.endPlateConnections()[0].params.deletedBolts[1] == 4,
              "round-trip: deletedBolts {2,4}");
        check(d4.endPlateConnections()[0].params.boltStandard == "ISO 4014",
              "round-trip: boltStandard");
        check(d4.endPlateConnections()[0].params.threadInMat,
              "round-trip: threadInMat");
        check(d4.endPlateConnections()[0].params.siteWorkshop == 1,
              "round-trip: siteWorkshop");
        check(d4.endPlateConnections()[0].params.boltCutLengthMode == 1,
              "round-trip: cutLengthMode");
        check(approx(d4.endPlateConnections()[0].params.boltLengthIncrease, 15.0),
              "round-trip: lengthIncrease");
        check(d4.endPlateConnections()[0].params.boltDirection == 2,
              "round-trip: boltDirection");
        check(d4.endPlateConnections()[0].params.boltComment == "test yorum",
              "round-trip: boltComment");
        check(approx(d4.endPlateConnections()[0].params.upperEdgeDistance, 15.0),
              "round-trip: upperEdgeDistance 15");
        check(approx(d4.endPlateConnections()[0].params.lowerEdgeDistance, -5.0),
              "round-trip: lowerEdgeDistance -5");
        check(approx(d4.endPlateConnections()[0].params.plateWidthOverride, 450.0),
              "round-trip: plateWidthOverride 450");
        check(approx(d4.endPlateConnections()[0].params.plateHeightOverride, 320.0),
              "round-trip: plateHeightOverride 320");
        check(d4.endPlateConnections()[0].params.plateMaterial == "S235JR",
              "round-trip: plateMaterial S235JR");
        const auto& rp = d4.endPlateConnections()[0].params;
        check(rp.boltSpacingXs.size() == 1u, "round-trip: boltSpacingXs boyut 1");
        if (rp.boltSpacingXs.size() == 1u)
            check(approx(rp.boltSpacingXs[0], 110.0), "round-trip: boltSpacingXs[0] 110");
        check(rp.boltSpacingYs.size() == 1u, "round-trip: boltSpacingYs boyut 1");
        if (rp.boltSpacingYs.size() == 1u)
            check(approx(rp.boltSpacingYs[0], 130.0), "round-trip: boltSpacingYs[0] 130");
    }

    std::printf("end_plate_test: %d check, %d failure\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
