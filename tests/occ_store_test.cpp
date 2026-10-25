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
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
    // Surum sorgulari: OCC'siz gozetleyici hicbir gercek surum tutmaz.
    VersionId activeVersion(EntityId) const noexcept override { return kInvalidVersionId; }
    bool findVersionExists(EntityId, VersionId version) const override {
        return version == kInvalidVersionId;
    }
    bool restoreActive(EntityId, VersionId version) override {
        return version == kInvalidVersionId;
    }
    VersionId baseVersion(EntityId) const noexcept override { return kInvalidVersionId; }
    bool setBaseVersion(EntityId, VersionId) override { return false; }
    std::size_t prune(const std::function<bool(EntityId, VersionId)>&) override {
        ++pruneCalls;
        return 0;
    }
    std::string serializeActive(EntityId) const override { return {}; }
    bool restoreActiveSolid(EntityId, const std::string&, std::string*) override { return false; }
    std::string serializeBase(EntityId) const override { return {}; }
    bool restoreBaseSolid(EntityId, const std::string&, std::string*) override { return false; }
    std::string occVersionString() const noexcept override { return {}; }
    int bRepFormatVersion() const noexcept override { return 0; }
    std::unique_ptr<OccStore> cloneEmpty() const override { return std::make_unique<SpyStore>(); }
    std::size_t clearCalls{0};
    std::size_t pruneCalls{0};
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
    // MMW8 yukleme store'u ATOMIK olarak DEGISTIRIR: cloneEmpty ile TAZE store
    // uretilir, basari olursa devralinir; eski store (ve `raw`) YOK EDILIR.
    // Bu yuzden raw'a dokunma (dangling) — yeni store'un durumunu dogrula.
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

// SURUM-FARKINDA gozetleyici: undo/redo'nun magaza uzerinde HANGI surumu
// etkinlestirdigini kaydeder. Gercek OCCT magazasinin yerine GECMEZ; amac
// Document'in undo/redo SOZLESMESINI olcmektir (Phase 3.2B).
class VersionedSpyStore final : public OccStore {
public:
    void clear() noexcept override { live = 0; }
    std::size_t size() const noexcept override { return live; }
    VersionId activeVersion(EntityId) const noexcept override { return active; }
    bool findVersionExists(EntityId, VersionId v) const override {
        return v == kInvalidVersionId || existing.count(v) != 0;
    }
    bool restoreActive(EntityId, VersionId v) override {
        if (v != kInvalidVersionId && existing.count(v) == 0) return false;
        active = v;
        ++restoreCalls;
        lastRestored = v;
        return true;
    }
    VersionId baseVersion(EntityId) const noexcept override { return kInvalidVersionId; }
    bool setBaseVersion(EntityId, VersionId) override { return false; }
    std::size_t prune(const std::function<bool(EntityId, VersionId)>&) override {
        ++pruneCalls;
        return 0;
    }
    std::string serializeActive(EntityId) const override { return {}; }
    bool restoreActiveSolid(EntityId, const std::string&, std::string*) override { return false; }
    std::string serializeBase(EntityId) const override { return {}; }
    bool restoreBaseSolid(EntityId, const std::string&, std::string*) override { return false; }
    std::string occVersionString() const noexcept override { return {}; }
    int bRepFormatVersion() const noexcept override { return 0; }
    std::unique_ptr<OccStore> cloneEmpty() const override { return std::make_unique<VersionedSpyStore>(); }
    std::size_t live{0};
    VersionId active{kInvalidVersionId};
    std::set<VersionId> existing;
    int restoreCalls{0};
    int pruneCalls{0};
    VersionId lastRestored{kInvalidVersionId};
};

void testGeometryDeltaAutoOpensRecord() {
    std::printf("Test 6: geometri deltasi kayit yokken KENDI kaydini acar; undo uygular\n");
    Document doc;
    auto store = std::make_unique<VersionedSpyStore>();
    VersionedSpyStore* raw = store.get();
    doc.setOccStore(std::move(store));
    raw->existing.insert(7);
    raw->existing.insert(9);
    raw->active = 7;

    check(!doc.hasOpenUndoRecord(), "baslangicta acik kayit yok");
    // Kayit ACMADAN delta kaydet → Document KENDI kaydini acmali.
    doc.recordGeometryDelta(42, 7, 9);
    check(doc.hasOpenUndoRecord(), "recordGeometryDelta kendi kaydini acti");
    check(doc.canUndo(), "geometri deltasi undo edilebilir (sessiz dusme YOK)");
    check(raw->restoreCalls == 0, "henuz restore cagrisi yok");

    check(doc.undo(), "undo true doner");
    check(raw->restoreCalls == 1 && raw->lastRestored == 7,
          "undo ONCESI surumu (7) restoreActive ile geri yukledi");
    check(!doc.canUndo(), "kayit tukendi");

    check(doc.redo(), "redo true doner");
    check(raw->lastRestored == 9, "redo SONRAKI surumu (9) geri yukledi");
}

void testUndoRejectedOnMissingVersion() {
    std::printf("Test 7: EKSIK surum -> undo REDDEDILIR, hicbir sey degismez\n");
    Document doc;
    auto store = std::make_unique<VersionedSpyStore>();
    VersionedSpyStore* raw = store.get();
    doc.setOccStore(std::move(store));
    // ONCESI var olmayan bir surume isaret ediyor.
    doc.recordGeometryDelta(5, 999u, kInvalidVersionId);
    check(doc.canUndo(), "kayit yiginda");
    check(!doc.undo(), "undo REDDEDILDI (eksik surum)");
    check(raw->restoreCalls == 0, "reddedilen undo magazaya DOKUNMADI");
    check(doc.canUndo(), "reddedilen kayit yigindan TUKETILMEDI");
}

void testDeltaInvalidVersionIsValid() {
    std::printf("Test 8: kInvalidVersionId 'geometri yok' GECERLI (olusturma-undo)\n");
    Document doc;
    auto store = std::make_unique<VersionedSpyStore>();
    VersionedSpyStore* raw = store.get();
    doc.setOccStore(std::move(store));
    raw->existing.insert(3);
    doc.recordGeometryDelta(11, kInvalidVersionId, 3);
    check(doc.undo(), "kInvalid 'before' ile undo KABUL edilir (olusturma-undo)");
    check(raw->lastRestored == kInvalidVersionId, "'geometri yok' haline donuldu");
}

// SURUM BUDAMA (Phase 3.2C) — Document'in BUDAMA SOZLESMESI.
// (varlik, surum) ciftlerini GERCEKTEN modelleyen gozetleyici: Document'in
// hesapladigi keep yuklemi DOGRU mu ve budama DOGRU tetikleniyor mu?
class PruningSpyStore final : public OccStore {
public:
    void clear() noexcept override { existing.clear(); active.clear(); base.clear(); }
    std::size_t size() const noexcept override { return existing.size(); }
    VersionId activeVersion(EntityId e) const noexcept override {
        auto it = active.find(e);
        return it == active.end() ? kInvalidVersionId : it->second;
    }
    bool findVersionExists(EntityId e, VersionId v) const override {
        if (v == kInvalidVersionId) return true;
        auto it = existing.find(e);
        return it != existing.end() && it->second.count(v) != 0;
    }
    bool restoreActive(EntityId e, VersionId v) override {
        if (v != kInvalidVersionId && !findVersionExists(e, v)) return false;
        active[e] = v;
        return true;
    }
    VersionId baseVersion(EntityId e) const noexcept override {
        auto it = base.find(e);
        return it == base.end() ? kInvalidVersionId : it->second;
    }
    bool setBaseVersion(EntityId e, VersionId v) override {
        if (v == kInvalidVersionId) { base[e] = kInvalidVersionId; return true; }
        if (!findVersionExists(e, v)) return false;
        base[e] = v;
        return true;
    }
    // Document'in gonderdigi keep yuklemine gore GERCEKTEN budar; her cagrida
    // keep'in hangi (varlik, surum) ciftleri icin TRUE dondugunu kaydeder.
    std::size_t prune(const std::function<bool(EntityId, VersionId)>& keep) override {
        ++pruneCalls;
        std::size_t pruned = 0;
        for (auto& [e, versions] : existing) {
            for (auto it = versions.begin(); it != versions.end();) {
                if (!keep(e, *it)) { it = versions.erase(it); ++pruned; }
                else { kept.emplace(e, *it); ++it; }
            }
            auto a = active.find(e);
            if (a != active.end() && a->second != kInvalidVersionId &&
                versions.count(a->second) == 0)
                a->second = kInvalidVersionId;
            // Taban surumu da budanmissa asili kalmasin (Document korur, savunma).
            auto b = base.find(e);
            if (b != base.end() && b->second != kInvalidVersionId &&
                versions.count(b->second) == 0)
                b->second = kInvalidVersionId;
        }
        return pruned;
    }
    // Varligin etkin surumunu DOGRUDAN ayarla (test kurulumu).
    void setActive(EntityId e, VersionId v) {
        if (v != kInvalidVersionId) existing[e].insert(v);
        active[e] = v;
    }
    // Varligin TABAN surumunu DOGRUDAN ayarla (test kurulumu).
    void setBase(EntityId e, VersionId v) {
        if (v != kInvalidVersionId) existing[e].insert(v);
        base[e] = v;
    }
    std::string serializeActive(EntityId) const override { return {}; }
    bool restoreActiveSolid(EntityId, const std::string&, std::string*) override { return false; }
    std::string serializeBase(EntityId) const override { return {}; }
    bool restoreBaseSolid(EntityId, const std::string&, std::string*) override { return false; }
    std::string occVersionString() const noexcept override { return {}; }
    int bRepFormatVersion() const noexcept override { return 0; }
    std::unique_ptr<OccStore> cloneEmpty() const override { return std::make_unique<PruningSpyStore>(); }
    std::map<EntityId, std::set<VersionId>> existing;
    std::map<EntityId, VersionId> active;
    std::map<EntityId, VersionId> base;
    std::set<std::pair<EntityId, VersionId>> kept;
    std::size_t pruneCalls{0};
};

void testPruneOnClearHistory() {
    std::printf("Test 9: clearHistory budama TETIKLER; canli etkin surum KORUNUR\n");
    Document doc;
    auto store = std::make_unique<PruningSpyStore>();
    PruningSpyStore* raw = store.get();
    doc.setOccStore(std::move(store));

    // Canli varlik (kimlik 1) + etkin surum v5.
    doc.addModel(WireframeModel::line({0, 0, 0}, {1, 0, 0}));
    const EntityId e = doc.modelId(0);
    raw->setActive(e, 5);

    doc.clearHistory();
    check(raw->pruneCalls == 1, "clearHistory() budama tetikledi (bir kez)");
    check(raw->kept.count({e, 5}) != 0,
          "canli varligin etkin surumu keep kumesinde KORUNDU");
    check(raw->existing.count(e) != 0 && raw->existing[e].count(5) != 0,
          "canli etkin surum gercekten sakli kaldi");
}

void testPruneDropsDeadVersions() {
    std::printf("Test 10: referanssiz (olu) surumler budanir; referansli KORUNUR\n");
    Document doc;
    auto store = std::make_unique<PruningSpyStore>();
    PruningSpyStore* raw = store.get();
    doc.setOccStore(std::move(store));

    doc.addModel(WireframeModel::line({0, 0, 0}, {1, 0, 0}));
    const EntityId e = doc.modelId(0);
    // v1 = olu (eski), v5 = etkin (canli).
    raw->existing[e].insert(1);
    raw->setActive(e, 5);

    doc.clearHistory();
    check(raw->existing[e].count(5) != 0, "etkin surum (v5) korundu");
    check(raw->existing[e].count(1) == 0, "referanssiz eski surum (v1) budandi");
}

void testBaseVersionSurvivesPrune() {
    std::printf("Test 12: kesme taban surumu budamadan KORUNUR (pre-cut B-Rep)\n");
    Document doc;
    auto store = std::make_unique<PruningSpyStore>();
    PruningSpyStore* raw = store.get();
    doc.setOccStore(std::move(store));

    doc.addModel(WireframeModel::line({0, 0, 0}, {1, 0, 0}));
    const EntityId e = doc.modelId(0);
    // v1 = taban (pre-cut), v2 = referanssiz (olu), v5 = etkin (kesilmis).
    raw->setActive(e, 5);       // aktif v5'i ekler
    raw->setBase(e, 1);         // taban v1'i ekler
    raw->existing[e].insert(2); // olu surum

    doc.clearHistory();  // budama tetikler
    check(raw->existing[e].count(1) != 0, "taban surum (v1) budamadan korundu");
    check(raw->kept.count({e, 1}) != 0, "taban surum keep kumesinde");
    check(raw->baseVersion(e) == 1, "baseVersion hala v1");
    check(raw->existing[e].count(5) != 0, "etkin surum (v5) korundu");
    check(raw->existing[e].count(2) == 0, "referanssiz surum (v2) budandi");
}

void testPruneOnUndoLimit() {
    std::printf("Test 11: 100 kayit siniri asilinca budama TETIKLENIR\n");
    Document doc;
    auto store = std::make_unique<SpyStore>();
    SpyStore* raw = store.get();
    doc.setOccStore(std::move(store));
    // 105 snapshot: 100'u astiginda pop_front + budama calisir.
    for (int i = 0; i < 105; ++i) doc.pushSnapshot();
    check(raw->pruneCalls > 0, "100 kayit siniri asilinca budama cagrildi");
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
    testGeometryDeltaAutoOpensRecord();
    testUndoRejectedOnMissingVersion();
    testDeltaInvalidVersionIsValid();
    testPruneOnClearHistory();
    testPruneDropsDeadVersions();
    testBaseVersionSurvivesPrune();
    testPruneOnUndoLimit();
    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OCC STORE (Document): OK\n");
    return g_failures == 0 ? 0 : 1;
}
