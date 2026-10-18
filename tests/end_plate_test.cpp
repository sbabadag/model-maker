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
    }

    std::printf("end_plate_test: %d check, %d failure\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
