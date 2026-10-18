// GERCEK TRANSACTION ROLLBACK (Slice 2d-2c P0) — OCC'siz core test.
//
// Document::rollbackOpenRecord, basarisiz bir islemin (profil degisimi +
// hareket gibi) belge durumunu, model listesini ve undo/redo yigini TUTARLI
// birakmasini dogrular: geri alinir, redo'ya eklenmez, onceki kayitlar korunur.
//
// windows.h yok; Linux'ta kosar.

#include "model_maker/document.hpp"
#include "model_maker/occ_store.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>

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

// Surum defteri tutan MINIMAL sahte store (OCCT YOK) — Document'in base/active
// surum geri getirmesini (restoreActive/setBaseVersion cagrilarini) dogrulamak
// icin. Gercek OccSolidStore'un yalnizca surum-isaretci kismini modeler.
struct FakeOccStore : OccStore {
    std::map<EntityId, std::vector<VersionId>> versions;
    std::map<EntityId, VersionId> active;
    std::map<EntityId, VersionId> base;

    void seed(EntityId id, VersionId v) { versions[id].push_back(v); }
    void setActive(EntityId id, VersionId v) { active[id] = v; }
    void setBase(EntityId id, VersionId v) { base[id] = v; }
    bool has(EntityId id, VersionId v) const {
        const auto it = versions.find(id);
        if (it == versions.end()) return false;
        return std::find(it->second.begin(), it->second.end(), v) != it->second.end();
    }

    void clear() noexcept override { versions.clear(); active.clear(); base.clear(); }
    std::size_t size() const noexcept override { return versions.size(); }
    VersionId activeVersion(EntityId id) const noexcept override {
        const auto it = active.find(id);
        return it == active.end() ? kInvalidVersionId : it->second;
    }
    bool findVersionExists(EntityId id, VersionId version) const override {
        if (version == kInvalidVersionId) return true;  // "geometri yok" gecerli
        return has(id, version);
    }
    bool restoreActive(EntityId id, VersionId version) override {
        if (version == kInvalidVersionId) { active[id] = kInvalidVersionId; return true; }
        if (!has(id, version)) return false;
        active[id] = version;
        return true;
    }
    VersionId baseVersion(EntityId id) const noexcept override {
        const auto it = base.find(id);
        return it == base.end() ? kInvalidVersionId : it->second;
    }
    bool setBaseVersion(EntityId id, VersionId version) override {
        if (version == kInvalidVersionId) { base[id] = kInvalidVersionId; return true; }
        if (!has(id, version)) return false;
        base[id] = version;
        return true;
    }
    std::size_t prune(const std::function<bool(EntityId, VersionId)>&) override { return 0; }
    std::string serializeActive(EntityId) const override { return {}; }
    bool restoreActiveSolid(EntityId, const std::string&, std::string*) override { return false; }
    std::string serializeBase(EntityId) const override { return {}; }
    bool restoreBaseSolid(EntityId, const std::string&, std::string*) override { return false; }
    std::string occVersionString() const noexcept override { return {}; }
    int bRepFormatVersion() const noexcept override { return 0; }
    std::unique_ptr<OccStore> cloneEmpty() const override {
        return std::unique_ptr<OccStore>(new FakeOccStore());
    }
};

int main() {
    Document doc;

    // Onceki (bagimsiz) islem: bir dikdortgen ekle — rollback'ten ETKILENMEMELI.
    doc.pushSnapshot();
    doc.addModel(WireframeModel::rectangle({0, 0, 0}, {100, 100, 0}));
    const EntityId id0 = doc.modelIds().back();
    check(id0 != kInvalidEntityId, "dikdortgen eklendi");

    // BASArisiz OLACAK islem: profil degisimi + hareket (tek acik kayit).
    const Vec3 posBefore = doc.models()[0].vertices()[0];
    doc.pushSnapshot();
    const std::size_t assigned = doc.setModelProfile({0}, "HEB300");
    check(assigned == 1, "profil atandi (HEB300)");
    doc.moveModels({0}, {10.0, 20.0, 30.0});

    // Islem uygulandi.
    check(doc.models()[0].properties().profileName == "HEB300", "profil HEB300");
    const Vec3 posAfter = doc.models()[0].vertices()[0];
    check(posAfter.x > posBefore.x, "model tasindi (+X)");

    // ROLLBACK: islem tamamen geri alinmali, redo'ya eklenmemeli.
    const bool ok = doc.rollbackOpenRecord();
    check(ok, "rollbackOpenRecord true");
    check(doc.models()[0].properties().profileName.empty(), "profil geri alindi (bos)");
    const Vec3 posRolled = doc.models()[0].vertices()[0];
    check(std::abs(posRolled.x - posBefore.x) < 1e-9 &&
              std::abs(posRolled.y - posBefore.y) < 1e-9 &&
              std::abs(posRolled.z - posBefore.z) < 1e-9,
          "konum geri alindi (onceki konum)");
    check(!doc.canRedo(), "redo YOK (basarisiz islem redo gecmisine eklenmedi)");
    check(doc.canUndo(), "onceki kayit (addModel) korunur — undo edilebilir");

    // Onceki kayit hala calisir: undo -> dikdortgen silinir.
    check(doc.undo(), "onceki kayit undo edilebilir");
    check(doc.models().empty(), "undo sonrasi model yok");

    // Acik kayit yokken rollback -> false, hicbir sey degismez.
    check(!doc.rollbackOpenRecord(), "acik kayit yokken rollback false");

    // --- TABAN (BASE) SURUM GERI GETIRME (Slice 2d-2c) -----------------------
    // rebuildCutBaseSolid taban (pre-cut) surumunu degistirir; bu degisiklik
    // undo/redo/rollback'te GERI gelmeli (yoksa kesme yanlis tabandan turer).
    {
        Document d;
        FakeOccStore* s = new FakeOccStore();
        d.setOccStore(std::unique_ptr<OccStore>(s));
        const EntityId e = 42;
        s->seed(e, 1); s->seed(e, 2); s->seed(e, 3);
        s->setActive(e, 1); s->setBase(e, 1);

        // Komut: putOccSolid (aktif 1->2) + rebuildCutBase (taban 1->2).
        d.pushSnapshot();
        d.recordGeometryDelta(e, 1, 2);
        d.recordBaseVersionDelta(e, 1, 2);
        s->setActive(e, 2); s->setBase(e, 2);  // mutasyonu uygula (simulasyon)
        check(s->activeVersion(e) == 2 && s->baseVersion(e) == 2,
              "base: islem sonrasi aktif=2 taban=2");

        check(d.rollbackOpenRecord(), "base: rollback true");
        check(s->activeVersion(e) == 1, "base: rollback aktif surumu geri getirdi");
        check(s->baseVersion(e) == 1, "base: rollback TABAN surumunu geri getirdi (kritik)");

        // Yeniden uygula; UNDO/REDO ile taban da geri gelsin.
        d.pushSnapshot();
        d.recordGeometryDelta(e, 1, 2);
        d.recordBaseVersionDelta(e, 1, 2);
        s->setActive(e, 2); s->setBase(e, 2);
        check(d.undo(), "base: undo true");
        check(s->activeVersion(e) == 1 && s->baseVersion(e) == 1,
              "base: undo aktif+taban geri getirdi");
        check(d.redo(), "base: redo true");
        check(s->activeVersion(e) == 2 && s->baseVersion(e) == 2,
              "base: redo aktif+taban ileri getirdi");
    }

    // --- REDO YIGINI TUTARLI KALIR (Slice 2d-2c) ----------------------------
    // Basarisiz komut rollback edilince: kayit redo'ya EKLENMEZ, onceki komut
    // undo edilebilir kalir. Redo gecmisi yeni komut basinda standart undo
    // semantigi geregi temizlendi — rollback onu GERI GETIRMEZ (budama
    // degismezi korunur), yalnizca basarisiz kaydi redo'ya EKLEMEZ.
    {
        Document d;
        d.pushSnapshot();
        d.addModel(WireframeModel::rectangle({0, 0, 0}, {100, 100, 0}));
        d.pushSnapshot();
        d.moveModels({0}, {100.0, 0.0, 0.0});  // basarisiz olacak komut
        check(d.rollbackOpenRecord(), "redo: rollback true");
        check(!d.canRedo(), "redo: basarisiz kayit redo'ya EKLENMEDI");
        check(d.canUndo(), "redo: onceki kayit undo edilebilir kalir");
        const double x = d.models()[0].vertices()[0].x;
        check(std::abs(x - 0.0) < 1e-9, "redo: konum rollback sonrasi baslangica dondu");
    }

    std::printf("document_transaction: %d kontrol, %d basarisiz\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
