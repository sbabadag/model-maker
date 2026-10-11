// TEKLA-TARZI YAPI GRIDI cekirdek testi (OCC GEREKTIRMEZ, Linux'ta kosar).
//
// Kullanici sarti: "grid objemizi birebir Tekla'nin calisma sistemiyle ayni
// ayarlayalim" (Tekla Structures 2026, Work with grids / Create a grid /
// Grid properties). Bu test Tekla'nin DAVRANISINI kilitler:
//   * koordinat dili: boslukla ayrilmis degerler, "N*deger" tekrari,
//     X/Y ARTIMSAL (kumulatif), Z MUTLAK
//   * Labels: X kutusu Y'ye paralel cizgileri, Y kutusu X'e paralel
//     cizgileri adlandirir; bos birakilabilir
//   * Line extensions: Left/Below ve Right/Above AYRI
//   * Origin: is duzleminden offset
//   * cizim geometrisi SPESIFIKASYONDAN turetilir (axes turevdir)
//   * MMW7 kalicilik + MMW5/6 (yalniz-axes) geriye donuk uyumluluk
//   * grid ekle/degistir/sil GERI ALINABILIR (GridAdd/GridRemove/GridReplace)
#include "model_maker/document.hpp"
#include "model_maker/grid.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
static bool eqVec(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!near(a[i], b[i])) return false;
    return true;
}

// Bir aks cizgisini (X konumu, Y konumu) ile bulur.
static const GridAxisLine* verticalAt(const GridDefinition& g, double x) {
    for (const auto& a : g.axes)
        if (!a.horizontal && near(a.from.x, x)) return &a;
    return nullptr;
}
static const GridAxisLine* horizontalAt(const GridDefinition& g, double y) {
    for (const auto& a : g.axes)
        if (a.horizontal && near(a.from.y, y)) return &a;
    return nullptr;
}

int main() {
    // 1) TEKLA KOORDINAT DILI: "0 2*4000" ve "0 4000 4000" AYNI sonucu verir
    //    (dokuman: ikisi de 4000 aralikli uc cizgi uretir).
    {
        const auto star = parseGridCoordinateLine("0 2*4000", true);
        const auto plain = parseGridCoordinateLine("0 4000 4000", true);
        check(eqVec(star, {0.0, 4000.0, 8000.0}), "N*deger tekrari: 0 2*4000 -> {0,4000,8000}");
        check(eqVec(star, plain), "\"0 2*4000\" == \"0 4000 4000\"");
    }
    // 2) Z MUTLAK (kumulatif DEGIL).
    {
        const auto z = parseGridCoordinateLine("0 3600 7200", false);
        check(eqVec(z, {0.0, 3600.0, 7200.0}), "Z mutlak: 0 3600 7200 -> {0,3600,7200}");
    }
    // 3) Virgullu ondalik + gecersiz token atlanir.
    {
        const auto v = parseGridCoordinateLine("1,5 2,5", false);
        check(eqVec(v, {1.5, 2.5}), "virgullu ondalik kabul edilir");
        const auto bad = parseGridCoordinateLine("abc 1000 xyz", true);
        check(eqVec(bad, {1000.0}), "gecersiz token sessizce atlanir");
        check(parseGridCoordinateLine("", true).empty(), "bos satir -> bos liste");
    }
    // 4) Etiket dili + otomatik adlar.
    {
        const auto labels = parseGridLabelLine("1 2 3 4");
        check(labels.size() == 4 && labels[0] == "1" && labels[3] == "4", "etiket satiri ayristirilir");
        check(gridAutoNumberLabel(0) == "1" && gridAutoNumberLabel(9) == "10", "X otomatik: sayi");
        check(gridAutoLetterLabel(0) == "A" && gridAutoLetterLabel(25) == "Z" &&
                  gridAutoLetterLabel(26) == "AA", "Y otomatik: harf (A..Z, AA)");
    }
    // 5) TURETME: X konumlari DUKEY, Y konumlari YATAY aks; Z kotlari kat
    //    duzlemi kenarlari. Uzanti Left/Below - Right/Above AYRI.
    {
        GridDefinition g;
        g.xCoords = {0.0, 6000.0, 12000.0};
        g.yCoords = {0.0, 5000.0};
        g.zCoords = {0.0};
        g.xLabels = {"1", "2", "3"};
        g.yLabels = {"A", "B"};
        g.zLabels = {"0"};
        g.extendLeft = 500.0;
        g.extendRight = 1500.0;
        g.extendBelow = 800.0;
        g.extendAbove = 2000.0;
        rebuildGridAxes(g);
        // 3 dikey + 2 yatay + 1 kot icin 2 kenar = 7
        check(g.axes.size() == 7, "turetme: 3 dikey + 2 yatay + 2 kot kenari");

        const GridAxisLine* v1 = verticalAt(g, 6000.0);
        check(v1 != nullptr, "X konumu dikey aks uretir");
        if (v1) {
            check(near(v1->from.y, -800.0) && near(v1->to.y, 5000.0 + 2000.0),
                  "dikey aks Y boyunca Below..spanY+Above uzar (uzanti AYRI)");
            check(v1->label == "2", "X kutusu Y'ye paralel cizgiyi adlandirir");
            check(near(v1->from.z, 0.0) && near(v1->to.z, 0.0), "aks grid duzleminde (z=origin.z)");
        }
        const GridAxisLine* h1 = horizontalAt(g, 0.0);
        check(h1 != nullptr, "Y konumu yatay aks uretir");
        if (h1) {
            check(near(h1->from.x, -500.0) && near(h1->to.x, 12000.0 + 1500.0),
                  "yatay aks X boyunca Left..spanX+Right uzar");
            check(h1->label == "A", "Y kutusu X'e paralel cizgiyi adlandirir");
        }
        // Kot kenarlari: on (y = -below), arka (y = spanY + above).
        int front = 0, back = 0;
        for (const auto& a : g.axes)
            if (a.horizontal) {
                if (near(a.from.y, -800.0) && near(a.from.z, 0.0)) ++front;
                if (near(a.from.y, 5000.0 + 2000.0) && near(a.from.z, 0.0)) ++back;
            }
        check(front == 1 && back == 1, "kot duzlemi on/arka kenar cizgileri");
    }
    // 6) ORIGIN offset: tum akslar origin kadar kayar.
    {
        GridDefinition g;
        g.xCoords = {0.0, 6000.0};
        g.yCoords = {0.0};
        g.origin = Vec3{1000.0, 2000.0, 3000.0};
        rebuildGridAxes(g);
        const GridAxisLine* v = verticalAt(g, 1000.0);
        const GridAxisLine* v2 = verticalAt(g, 7000.0);
        check(v != nullptr && v2 != nullptr, "origin offset akslari kaydirir");
        check(near(g.axes.front().from.z, 3000.0), "origin.z aksa yansir");
    }
    // 7) Etiket tamamlama: liste DOLU ama kisa ise tamamlanir; liste BOS ise
    //    dokunulmaz (Tekla: etiket kutusu bos birakilabilir).
    {
        GridDefinition g;
        g.xCoords = {0.0, 1000.0, 2000.0};
        g.xLabels = {"1"};
        ensureGridLabels(g);
        check(g.xLabels.size() == 3 && g.xLabels[1] == "2" && g.xLabels[2] == "3",
              "eksik etiketler otomatik tamamlanir");
        GridDefinition e;
        e.xCoords = {0.0, 1000.0};
        ensureGridLabels(e);
        check(e.xLabels.empty(), "tamamen bos etiket kutusu etiketsiz kalir");
    }
    // 8) Document: grid ekle/degistir/sil + GERI ALINABILIR.
    //    (Grid mutasyonlari KENDI undo op'unu kaydeder; cagiran yalnizca
    //    pushSnapshot() ile kayit acar — model mutasyonlariyla ayni desen.)
    {
        Document doc;
        GridDefinition g;
        g.name = "AKSLAR";
        g.xCoords = {0.0, 6000.0};
        g.yCoords = {0.0};
        rebuildGridAxes(g);

        doc.pushSnapshot();
        doc.addGrid(g);
        check(doc.grids().size() == 1, "grid eklendi");

        doc.undo();
        check(doc.grids().empty(), "grid ekleme geri alinir (GridAdd)");
        doc.redo();
        check(doc.grids().size() == 1, "grid ekleme yeniden yapilir (redo)");

        // Degistir: GridReplace.
        GridDefinition after = doc.grids()[0];
        after.xCoords = {0.0, 6000.0, 12000.0};
        doc.pushSnapshot();
        doc.setGrid(0, after);
        check(doc.grids()[0].xCoords.size() == 3, "grid degistirildi");
        doc.undo();
        check(doc.grids()[0].xCoords.size() == 2, "grid degisikligi geri alinir (GridReplace)");
        doc.redo();
        check(doc.grids()[0].xCoords.size() == 3, "grid degisikligi yeniden yapilir");

        // Sil: GridRemove.
        doc.pushSnapshot();
        doc.removeGrid(0);
        check(doc.grids().empty(), "grid silindi");
        doc.undo();
        check(doc.grids().size() == 1 && doc.grids()[0].xCoords.size() == 3,
              "grid silme geri alinir (GridRemove)");
        doc.redo();
        check(doc.grids().empty(), "grid silme yeniden yapilir");
    }
    // 9) MMW7 kalicilik: spesifikasyon + turetilmis geometri aynen geri gelir.
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "mm_grid_roundtrip.mmw";
        Document doc;
        GridDefinition g;
        g.name = "BINA A";
        g.xCoords = {0.0, 6000.0, 12000.0};
        g.yCoords = {0.0, 5000.0};
        g.zCoords = {0.0, 3600.0};
        g.xLabels = {"1", "2", "3"};
        g.yLabels = {"A", "B"};
        g.zLabels = {"0", "3600"};
        g.extendLeft = 750.0;
        g.extendRight = 1250.0;
        g.extendBelow = 500.0;
        g.extendAbove = 1500.0;
        g.origin = Vec3{100.0, 200.0, 0.0};
        g.magnetic = true;
        g.color = 0x112233u;
        g.labelColor = 0x445566u;
        g.labelFontSize = 18;
        rebuildGridAxes(g);
        doc.addGrid(g);
        doc.addLine({0.0, 0.0, 0.0}, {1000.0, 0.0, 0.0});
        doc.save(path);

        Document loaded;
        loaded.load(path);
        check(loaded.grids().size() == 1, "MMW7: grid kaydedilip yuklendi");
        if (loaded.grids().size() == 1) {
            const GridDefinition& r = loaded.grids()[0];
            check(r.name == "BINA A", "MMW7: ad korunur");
            check(eqVec(r.xCoords, g.xCoords) && eqVec(r.yCoords, g.yCoords) &&
                      eqVec(r.zCoords, g.zCoords),
                  "MMW7: koordinat spesifikasyonu korunur");
            check(r.xLabels == g.xLabels && r.yLabels == g.yLabels && r.zLabels == g.zLabels,
                  "MMW7: etiketler korunur");
            check(near(r.extendLeft, 750.0) && near(r.extendAbove, 1500.0),
                  "MMW7: uzantilar (Left/Below - Right/Above) korunur");
            check(near(r.origin.x, 100.0) && near(r.origin.y, 200.0), "MMW7: origin korunur");
            check(r.magnetic && r.color == 0x112233u && r.labelColor == 0x445566u &&
                      r.labelFontSize == 18,
                  "MMW7: magnetic/renk/yazi boyu korunur");
            check(r.axes.size() == g.axes.size(), "MMW7: turetilmis geometri yeniden uretilir");
        }
        // GERIYE DONUK UYUMLULUK: MMW6 (yalniz-axes) grid blogu da okunur.
        // Eski bicim: '<len>:<name>' <visible> <axisCount> sonra her aks icin
        // from(3) to(3) horizontal '<len>:<label>'.
        const std::filesystem::path legacy =
            std::filesystem::temp_directory_path() / "mm_grid_legacy.mmw";
        {
            std::FILE* f = std::fopen(legacy.string().c_str(), "w");
            check(f != nullptr, "eski bicim dosyasi yazilabilir");
            if (f) {
                std::fprintf(f, "MMW6\n0\nG 1\n");
                std::fprintf(f, "4:GRID 1 2\n");
                std::fprintf(f, "0 0 0 0 5000 0 0 1:1\n");
                std::fprintf(f, "0 0 0 6000 0 0 1 1:A\n");
                std::fclose(f);
            }
        }
        Document legacyDoc;
        bool legacyOk = true;
        try {
            legacyDoc.load(legacy);
        } catch (...) {
            legacyOk = false;
        }
        check(legacyOk, "MMW6 (eski) grid blogu okunur — patlamaz");
        check(legacyDoc.grids().size() == 1, "eski grid yuklendi");
        if (legacyDoc.grids().size() == 1) {
            check(legacyDoc.grids()[0].axes.size() == 2, "eski grid akslari korunur");
            check(legacyDoc.grids()[0].xCoords.empty(),
                  "eski gridde spesifikasyon yok (yalniz turetilmis geometri)");
        }
        std::filesystem::remove(path);
        std::filesystem::remove(legacy);
    }

    // ---------------------------------------------------------------------
    // GORUNUS DUZLEMI (Tekla): grid bir 3B duzlemler kompleksidir ve bir
    // pencerede YALNIZCA gorunus duzlemini KESEN ailelerin kesisim cizgileri
    // cizilir. Gorunus duzlemine PARALEL aile cizilmez -> "3B grid" OLUSMAZ.
    // ---------------------------------------------------------------------
    {
        check(gridViewPlaneForDirection(Vec3{0, 0, 1}) == GridViewPlane::XY,
              "bakış +Z (ust) -> XY gridi");
        check(gridViewPlaneForDirection(Vec3{0, 0, -1}) == GridViewPlane::XY,
              "bakış -Z (alt) -> XY gridi");
        check(gridViewPlaneForDirection(Vec3{0, 1, 0}) == GridViewPlane::XZ,
              "bakış +Y (on/arka) -> XZ gridi");
        check(gridViewPlaneForDirection(Vec3{0, -1, 0}) == GridViewPlane::XZ,
              "bakış -Y -> XZ gridi");
        check(gridViewPlaneForDirection(Vec3{1, 0, 0}) == GridViewPlane::YZ,
              "bakış +X (yan) -> YZ gridi");
        check(gridViewPlaneForDirection(Vec3{-1, 0, 0}) == GridViewPlane::YZ,
              "bakış -X -> YZ gridi");
        check(gridViewPlaneForDirection(Vec3{1, 1, 1}) == GridViewPlane::XY,
              "izometrik (esitlikte Z) -> plan gridi (Tekla 3B gorunusu)");
        check(gridViewPlaneForDirection(Vec3{5, 0.2, 0.1}) == GridViewPlane::YZ,
              "baskin eksen: egik bakista X -> YZ gridi");

        GridDefinition g;
        g.xCoords = {0.0, 6000.0};
        g.yCoords = {0.0, 5000.0};
        g.zCoords = {0.0, 3600.0};
        g.xLabels = {"1", "2"};
        g.yLabels = {"A", "B"};
        g.zLabels = {"+0", "+3600"};
        g.extendLeft = 500.0;
        g.extendRight = 500.0;
        g.extendBelow = 500.0;
        g.extendAbove = 500.0;
        rebuildGridAxes(g);
        const std::size_t baseAxes = g.axes.size();

        // Aile siniflandirma: bir aks cizgisinin degisen tek koordinati yonu verir.
        const auto alongY = [](const GridAxisLine& a) { return near(a.from.x, a.to.x) && near(a.from.z, a.to.z); };
        const auto alongX = [](const GridAxisLine& a) { return near(a.from.y, a.to.y) && near(a.from.z, a.to.z); };
        const auto alongZ = [](const GridAxisLine& a) { return near(a.from.x, a.to.x) && near(a.from.y, a.to.y); };

        // --- PLAN (XY): X ve Y aileleri; KAT (Z) duzlemleri paralel -> YOK ---
        const auto xy = gridAxesForView(g, GridViewPlane::XY);
        int xyAlongY = 0, xyAlongX = 0, xyAlongZ = 0;
        for (const auto& a : xy) {
            if (alongY(a)) ++xyAlongY;
            else if (alongX(a)) ++xyAlongX;
            else if (alongZ(a)) ++xyAlongZ;
        }
        check(xy.size() == 4 && xyAlongY == 2 && xyAlongX == 2 && xyAlongZ == 0,
              "XY gorunusu: yalniz X+Y aileleri, kat cizgisi YOK");
        // Uzanti + etiket: X ailesi (Y boyunca) below/above ile uzar, X etiketi tasir.
        const auto* xyX0 = [&]() -> const GridAxisLine* {
            for (const auto& a : xy)
                if (alongY(a) && near(a.from.x, 0.0)) return &a;
            return nullptr;
        }();
        check(xyX0 != nullptr && near(xyX0->from.y, -500.0) && near(xyX0->to.y, 5500.0) &&
                  xyX0->label == "1" && !xyX0->horizontal,
              "XY: X aks cizgisi below/above ile uzar ve X etiketini tasir");

        // --- ON/ARKA (XZ): gorunus duzlemi Y = origin.y ---
        const auto xz = gridAxesForView(g, GridViewPlane::XZ);
        int xzVertical = 0, xzHorizontal = 0, xzOther = 0;
        for (const auto& a : xz) {
            if (alongZ(a)) ++xzVertical;
            else if (alongX(a)) ++xzHorizontal;
            else ++xzOther;
        }
        check(xz.size() == 4 && xzVertical == 2 && xzHorizontal == 2 && xzOther == 0,
              "XZ gorunusu: 2 dikey (Z boyunca) + 2 yatay (X boyunca); Y ailesi YOK");
        const auto* xzVert0 = [&]() -> const GridAxisLine* {
            for (const auto& a : xz)
                if (alongZ(a) && near(a.from.x, 0.0)) return &a;
            return nullptr;
        }();
        check(xzVert0 != nullptr && near(xzVert0->from.z, -500.0) && near(xzVert0->to.z, 4100.0),
              "XZ: dikey cizgi z araligini (Below/Above uzantisiyla) kapsar");
        const auto* xzLevel = [&]() -> const GridAxisLine* {
            for (const auto& a : xz)
                if (alongX(a) && near(a.from.z, 0.0)) return &a;
            return nullptr;
        }();
        check(xzLevel != nullptr && near(xzLevel->from.x, -500.0) && near(xzLevel->to.x, 6500.0) &&
                  xzLevel->label == "+0",
              "XZ: kat cizgisi X boyunca uzanir ve Z etiketini tasir");
        bool xzInPlane = true;
        for (const auto& a : xz) xzInPlane = xzInPlane && near(a.from.y, 0.0) && near(a.to.y, 0.0);
        check(xzInPlane, "XZ: tum cizgiler gorunus duzleminde (y = origin.y)");

        // --- YAN (YZ): gorunus duzlemi X = origin.x ---
        const auto yz = gridAxesForView(g, GridViewPlane::YZ);
        int yzVertical = 0, yzHorizontal = 0, yzOther = 0;
        for (const auto& a : yz) {
            if (alongZ(a)) ++yzVertical;
            else if (alongY(a)) ++yzHorizontal;
            else ++yzOther;
        }
        check(yz.size() == 4 && yzVertical == 2 && yzHorizontal == 2 && yzOther == 0,
              "YZ gorunusu: 2 dikey (Z boyunca) + 2 yatay (Y boyunca); X ailesi YOK");
        const auto* yzVertA = [&]() -> const GridAxisLine* {
            for (const auto& a : yz)
                if (alongZ(a) && near(a.from.y, 0.0)) return &a;
            return nullptr;
        }();
        check(yzVertA != nullptr && yzVertA->label == "A" && near(yzVertA->from.y, 0.0),
              "YZ: Y aks cizgisi dikey cizilir ve Y etiketini tasir");
        bool yzInPlane = true;
        for (const auto& a : yz) yzInPlane = yzInPlane && near(a.from.x, 0.0) && near(a.to.x, 0.0);
        check(yzInPlane, "YZ: tum cizgiler gorunus duzleminde (x = origin.x)");

        // --- TEK KAYNAK: cizim geometrisi taban turevden SADECE aile secer ---
        check(g.axes.size() == baseAxes && baseAxes == 8,
              "gridAxesForView grid.axes'e DOKUNMAZ (snap/zoom tabani sabit: 4 plan + 4 kat)");
        bool planMatchesBase = true;
        for (const auto& a : xy) {
            bool found = false;
            for (const auto& b : g.axes)
                if (near(a.from.x, b.from.x) && near(a.from.y, b.from.y) && near(a.from.z, b.from.z) &&
                    near(a.to.x, b.to.x) && near(a.to.y, b.to.y) && near(a.to.z, b.to.z) &&
                    a.label == b.label && a.horizontal == b.horizontal)
                    found = true;
            planMatchesBase = planMatchesBase && found;
        }
        check(planMatchesBase, "XY cizim aileleri grid.axes'in X/Y aileleriyle BIREBIR ayni");

        // Kat yoksa: ayni kot farkli aile (dusey) cizgilerde uzanti devreye girer.
        GridDefinition planOnly;
        planOnly.xCoords = {0.0, 4000.0};
        planOnly.yCoords = {0.0, 3000.0};
        rebuildGridAxes(planOnly);
        const auto planOnlyXZ = gridAxesForView(planOnly, GridViewPlane::XZ);
        check(planOnlyXZ.size() == 2, "kat yoksa XZ gorunusunde yalniz X dikey aileleri");
        check(!planOnlyXZ.empty() && near(planOnlyXZ[0].to.z - planOnlyXZ[0].from.z, 2000.0),
              "kat yokken dikey uzanti Below/Above'dan gelir (2*1000 mm)");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
