// MMW8 Document entegrasyonu (Phase 3.4C) — OCC-KAPALI (core) senaryolar.
// Gercek B-Rep round-trip'i OCC'li harness'te (run_occ_solid_ids.py) sinanir;
// bu dosya Document::save/load'un MMW8 davranisini OCCT OLMADAN kilitler:
//   * EntityId + nextEntityId + ozellik + katman + grid round-trip
//   * Basarili yukleme eski undo/redo gecmisini TEMIZLER
//   * Basarisiz yukleme mevcut Document'i DEGISTIRMEZ (atomiklik)
//   * MMW1 (legacy) ac -> MMW8 kaydet -> tekrar ac (yukari donusum)
#include "model_maker/document.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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

static std::filesystem::path tempPath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

static std::string readAllBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

// Dogrulanabilir icerikli bir Document kurar: 3 model (cizgi/dikdortgen/nokta),
// bir katman, bir grid ve zengin ozellikler.
static void buildRich(Document& doc) {
    EntityProperties layer;
    layer.layer = "STRUCT";
    layer.colorIndex = 4;
    layer.trueColor = 0x3366AAu;
    layer.lineType = layer.effectiveLineType = "CONTINUOUS";
    doc.setLayerProperties(layer);

    WireframeModel line = WireframeModel::line({0, 0, 0}, {5, 0, 0});
    EntityProperties p;
    p.layer = "STRUCT";
    p.profileName = "IPE200";
    p.material = "S355";
    p.colorIndex = 4;
    p.thickness = 2.0;
    p.description = "kiris";
    line.setProperties(std::move(p));
    doc.addModel(std::move(line));

    doc.addModel(WireframeModel::rectangle({0, 0, 0}, {10, 20, 0}));
    doc.addModel(WireframeModel::point({1, 2, 3}));

    GridDefinition grid;
    grid.name = "BINA A";
    grid.xCoords = {0.0, 4000.0};
    grid.yCoords = {0.0, 3000.0};
    grid.zCoords = {0.0, 3600.0};
    grid.xLabels = {"1", "2"};
    grid.yLabels = {"A", "B"};
    grid.zLabels = {"+0", "+3600"};
    rebuildGridAxes(grid);
    doc.addGrid(std::move(grid));
}

// 1) Tam round-trip: kimlikler, sayac, ozellikler, katman, grid korunur.
static void testRoundTrip() {
    std::printf("Test 1: MMW8 Document round-trip (EntityId + ozellik + grid)\n");
    const auto path = tempPath("mmw8_doc_roundtrip.mmw");
    Document doc;
    buildRich(doc);
    const EntityId id0 = doc.modelId(0), id1 = doc.modelId(1), id2 = doc.modelId(2);
    const EntityId next = doc.nextEntityId();
    doc.save(path);

    Document loaded;
    loaded.load(path);
    check(loaded.models().size() == 3, "3 model yuklendi");
    check(loaded.modelId(0) == id0 && loaded.modelId(1) == id1 && loaded.modelId(2) == id2,
          "EntityId'ler BIREBIR korunur");
    check(loaded.nextEntityId() == next, "nextEntityId korunur");
    check(loaded.models()[0].properties().profileName == "IPE200" &&
              loaded.models()[0].properties().material == "S355" &&
              loaded.models()[0].properties().description == "kiris",
          "zengin ozellikler korunur");
    check(loaded.layers().count("STRUCT") == 1, "katman korunur");
    check(loaded.grids().size() == 1 && loaded.grids()[0].name == "BINA A" &&
              !loaded.grids()[0].axes.empty() &&
              loaded.grids()[0].xCoords == std::vector<double>({0.0, 4000.0}),
          "grid (spec + turetilmis eksenler) korunur");

    // Kayit/ac sonrasi yeni geometri olusturulabilir (sayac catismasiz devam).
    loaded.addLine({0, 0, 0}, {7, 7, 0});
    check(loaded.modelId(3) > id2 && loaded.modelId(3) != kInvalidEntityId,
          "ac sonrasi yeni varlik catismasiz kimlik alir");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// 2) Basarili yukleme eski undo/redo gecmisini TEMIZLER.
static void testUndoClearedOnLoad() {
    std::printf("Test 2: basarili yukleme undo gecmisini temizler\n");
    const auto path = tempPath("mmw8_doc_undo.mmw");
    Document src;
    src.addModel(WireframeModel::line({0, 0, 0}, {1, 0, 0}));
    src.addModel(WireframeModel::line({0, 0, 0}, {2, 0, 0}));
    src.save(path);

    Document doc;
    doc.pushSnapshot();
    doc.addModel(WireframeModel::line({9, 9, 9}, {9, 9, 0}));
    doc.addModel(WireframeModel::line({9, 9, 9}, {9, 8, 0}));
    check(doc.canUndo(), "yukleme oncesi undo gecmisi var");

    doc.load(path);
    check(!doc.canUndo() && !doc.canRedo(), "basarili yukleme undo/redo gecmisini TEMIZLER");
    check(doc.models().size() == 2, "dosya icerigi yuklendi");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// 3) Basarisiz yukleme mevcut Document'i DEGISTIRMEZ (atomiklik).
static void testFailedLoadLeavesUnchanged() {
    std::printf("Test 3: basarisiz yukleme Document'i degistirmez\n");
    // Gecerli bir MMW8 yaz, sonra BOZ (ortadan kes) — bozuk dosya.
    const auto path = tempPath("mmw8_doc_corrupt.mmw");
    {
        Document d;
        buildRich(d);
        d.save(path);
    }
    {
        std::ifstream in(path, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), {});
        in.close();
        bytes.resize(bytes.size() / 2);  // ortadan kes
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    Document doc;
    buildRich(doc);
    doc.pushSnapshot();
    doc.addModel(WireframeModel::line({0, 0, 0}, {1, 0, 0}));  // undo gecmisi olsun
    const std::size_t modelsBefore = doc.models().size();
    const EntityId nextBefore = doc.nextEntityId();
    const bool canUndoBefore = doc.canUndo();

    bool threw = false;
    try {
        doc.load(path);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "bozuk dosya yuklemesi FIRLATIR");
    check(doc.models().size() == modelsBefore, "model sayisi degismedi");
    check(doc.nextEntityId() == nextBefore, "sayac degismedi");
    check(doc.canUndo() == canUndoBefore, "undo gecmisi degismedi");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// 4) MMW1 (legacy) ac -> MMW8 kaydet -> tekrar ac (yukari donusum + kimlik).
static void testLegacyUpgrade() {
    std::printf("Test 4: MMW1 ac -> MMW8 kaydet -> tekrar ac\n");
    const auto legacy = tempPath("mmw8_doc_legacy.mmw");
    const auto upgraded = tempPath("mmw8_doc_upgraded.mmw");
    // Minimal MMW1: 1 cizgi (2 vertex, 1 edge).
    {
        std::ofstream out(legacy);
        out << "MMW1\n1\n2 1 0 0 0\n0 0 0\n1 0 0\n0 1\n";
    }
    Document doc;
    doc.load(legacy);
    check(doc.models().size() == 1, "MMW1 yuklendi");

    doc.save(upgraded);  // MMW8 olarak kaydedilir
    {
        std::ifstream peek(upgraded);
        std::string sig;
        peek >> sig;
        check(sig == "MMW8", "MMW1 belgesi MMW8 olarak kaydedilir");
    }
    Document reloaded;
    reloaded.load(upgraded);
    check(reloaded.models().size() == 1, "yukari donusum round-trip");
    check(reloaded.modelId(0) != kInvalidEntityId, "kimlik atandi");

    std::error_code ec;
    std::filesystem::remove(legacy, ec);
    std::filesystem::remove(upgraded, ec);
}

// 5) Kayit guvenligi (Linux/POSIX): yeniden kayit mevcut dosyayi atomik degistirir;
//    basarisiz kayit (gecersiz yol) firlatir ve ORJINALI KORUR + gecici temiz.
static void testSaveSafety() {
    std::printf("Test 5: kayit guvenligi (yeniden kayit + basarisiz kayit)\n");
    const auto path = tempPath("mmw8_doc_safety.mmw");
    Document doc;
    buildRich(doc);
    doc.save(path);
    const std::string first = readAllBytes(path);
    check(!first.empty(), "ilk kayit basarili");

    // Yeniden kayit (mevcut dosya uzerine) -> atomik degistirme, icerik guncellenir.
    doc.addModel(WireframeModel::line({0, 0, 0}, {9, 9, 9}));
    doc.save(path);
    const std::string second = readAllBytes(path);
    check(first != second, "yeniden kayit dosyayi gunceller (mevcut hedef degistirilir)");

    // Basarisiz kayit: gecersiz dizin -> ofstream acilamaz, FIRLATIR; orijinal
    // dosya (onceki basarili kayit) DEGISMEDEN kalir.
    const auto bad = std::filesystem::path("/nonexistent_dir_xyz/mmw8.mmw");
    bool threw = false;
    try {
        doc.save(bad);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "gecersiz yola kayit FIRLATIR");
    check(readAllBytes(path) == second, "basarisiz kayitta orijinal dosya KORUNUR");
    check(!std::filesystem::exists(std::filesystem::path(path.string() + ".tmp")),
          "gecici dosya sizmadi (temiz)");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

int main() {
    testRoundTrip();
    testUndoClearedOnLoad();
    testFailedLoadLeavesUnchanged();
    testLegacyUpgrade();
    testSaveSafety();
    std::printf("MMW8 Document: checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
