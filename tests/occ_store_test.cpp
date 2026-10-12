// BELGE-SAHIPLI B-Rep MAGAZASI — platformdan BAGIMSIZ (OCC'siz) testler.
//
// KAPSAM (dikkat — durust sinirlama):
//   Bu test, Document'in MAGAZA SOZLESMESINI dogrular:
//     * varsayilan olarak no-op (null) magaza ile kurulur,
//     * Document TAŞINABILIR / KOPYALANAMAZ (PIMPL + unique_ptr),
//     * clear() ve load() magazayi SIFIRLAR (sahiplik Document'ta),
//     * magaza kimligi Document'tan alir (Application'da AYRI harita YOK).
//   GERCEK OCCT magazasinin (OccSolidStore) sekil semantigi BURADA sinanmaz —
//   o, gercek OCCT ile derlenen OCC harness'inde (tests/run_occ_solid_ids.py)
//   sinanir. Burada magaza yerine bir GOZETLEYICI (spy) kullanilir; amac
//   Document'in cagri sozlesmesini olcmektir, magaza davranisini taklit degil.
//
// OCC'siz surumler de bu testi kosar; hicbir OCCT basligi gerekmez.

#include "model_maker/document.hpp"
#include "model_maker/occ_store.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

using namespace mm;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// Document'in magaza UZERINDE hangi islemleri yaptigini kaydeden GOZETLEYICI.
// Gercek OCCT magazasinin yerine GECMEZ; yalnizca Document sozlesmesini olcer.
class SpyStore final : public OccStore {
public:
    void clear() noexcept override {
        ++clearCalls;
        live = 0;
    }
    std::size_t size() const noexcept override { return live; }
    std::size_t clearCalls{0};
    std::size_t live{0};
};

std::filesystem::path tempPath(const char* name) {
    auto dir = std::filesystem::temp_directory_path() / "mm_occ_store_test";
    std::filesystem::create_directories(dir);
    return dir / name;
}

void testNullStoreDefaults() {
    std::printf("Test 1: null (no-op) store varsayilan + Document kurulumu\n");
    auto store = makeNullOccStore();
    check(store != nullptr, "makeNullOccStore returns a store");
    check(store->size() == 0, "null store size 0");
    check(store->empty(), "null store empty");
    store->clear();
    check(store->size() == 0, "null store clear() is no-op");

    // Document varsayilan olarak no-op magaza ile kurulur → OCC'sizken kati
    // yollari guvenli (segfault yok, bos magaza).
    Document doc;
    check(doc.occStore().size() == 0, "fresh Document has empty store");
    check(doc.occStore().empty(), "fresh Document store empty");
}

void testMoveAndNoCopy() {
    std::printf("Test 2: Document TASINABILIR (PIMPL), KOPYALANAMAZ\n");
    // Kopya DERLEME ZAMANINDA yasak (asagidaki static_assert'ler).
    static_assert(!std::is_copy_constructible_v<Document>,
                  "Document KOPYALANAMAZ (magaza tek sahipli)");
    static_assert(!std::is_copy_assignable_v<Document>,
                  "Document kopya atanamaz");
    static_assert(std::is_move_constructible_v<Document>,
                  "Document TASINABILIR (DxfFile::read / optional<Document>)");
    static_assert(std::is_move_assignable_v<Document>,
                  "Document tasima atanabilir");
    check(true, "copy deleted / move available (static_assert)");

    // Gercek kullanim yolu: std::optional<Document> (pendingDxfDocument_).
    std::optional<Document> holder;
    {
        Document doc;
        doc.addModel(WireframeModel::line({0, 0, 0}, {1, 0, 0}));
        holder = std::move(doc);
    }
    check(holder.has_value(), "Document moved into optional");
    check(holder->models().size() == 1, "moved Document keeps content");
    check(holder->modelId(0) != kInvalidEntityId, "moved Document keeps id");
    holder.reset(); // yikici out-of-line: magaza da yok edilir (crash yok)
    check(!holder.has_value(), "optional reset destroys Document safely");
}

void testClearResetsStore() {
    std::printf("Test 3: clear() magazayi SIFIRLAR (sahiplik Document'ta)\n");
    // Gozetleyici magaza ENJEKTE edilir: Document'in clear()'i magazaya
    // ULASIYOR mu? (Eskiden Application ayri bir clearOccSolidStore()
    // cagirmak zorundaydi — iki yerde elle esleme riski.)
    Document doc;
    auto spy = std::make_unique<SpyStore>();
    SpyStore* raw = spy.get();
    doc.setOccStore(std::move(spy));
    doc.addModel(WireframeModel::line({0, 0, 0}, {1, 0, 0}));
    raw->live = 1; // magazada bir sekil varmis gibi
    check(doc.occStore().size() == 1, "store has one recorded shape");

    doc.clear();
    check(raw->clearCalls == 1, "Document::clear() called store->clear() once");
    check(doc.occStore().size() == 0, "store empty after Document::clear()");
    check(doc.models().empty(), "models empty after clear");
    check(doc.modelIds().empty(), "ids empty after clear");
}

void testLoadResetsStore() {
    std::printf("Test 4: load() magazayi SIFIRLAR; kayitli tel kafes geri gelir\n");
    const auto path = tempPath("roundtrip.mmw");
    {
        Document doc;
        doc.addModel(WireframeModel::line({0, 0, 0}, {5, 0, 0}));
        doc.addModel(WireframeModel::line({0, 0, 0}, {0, 5, 0}));
        doc.save(path);
    }

    Document doc;
    auto spy = std::make_unique<SpyStore>();
    SpyStore* raw = spy.get();
    doc.setOccStore(std::move(spy));
    doc.addModel(WireframeModel::line({0, 0, 0}, {1, 0, 0}));
    raw->live = 3; // onceki belgenin sekilleri
    check(doc.occStore().size() == 3, "store non-empty before load");

    doc.load(path);
    check(raw->clearCalls == 1, "Document::load() called store->clear() once");
    check(doc.occStore().size() == 0, "store EMPTY after load (no stale shapes)");
    check(doc.models().size() == 2, "loaded 2 models (MMW tel kafes)");
    check(doc.modelId(0) != kInvalidEntityId && doc.modelId(1) != kInvalidEntityId,
          "loaded models got stable ids");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void testSetOccStoreNullSafe() {
    std::printf("Test 5: setOccStore(nullptr) guvenli no-op magazaya duser\n");
    Document doc;
    doc.setOccStore(nullptr);
    check(doc.occStore().size() == 0, "nullptr store falls back to no-op");
    doc.addModel(WireframeModel::line({0, 0, 0}, {1, 0, 0}));
    doc.clear(); // crash olmamali
    check(doc.occStore().size() == 0, "clear() safe with fallback store");
}

}  // namespace

int main() {
    std::printf("== OCC STORE (Document sahipligi) =="
                " — platformdan bagimsiz, OCC'siz ==\n");
    testNullStoreDefaults();
    testMoveAndNoCopy();
    testClearResetsStore();
    testLoadResetsStore();
    testSetOccStoreNullSafe();
    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OCC STORE (Document): OK\n");
    return g_failures == 0 ? 0 : 1;
}
