#pragma once

// GERCEK OCCT B-Rep magazasi.
//
// YALNIZCA OCC-acik derleme birimleri (src/occ_solid_store.cpp, application.cpp
// MM_HAS_OCC dallari, OCC harness) bu basligi include eder; OCCT basliklari
// gerektirir. Document bu tipi GORMEZ — yalnizca abstract OccStore& uzerinden
// sahiplenir. Somut API'ye Application `asSolidStore()` ile erisir.
//
// ---------------------------------------------------------------------------
// SURUM MODELI (Phase 3.2A)
// ---------------------------------------------------------------------------
// Bir varligin (EntityId) B-Rep geometrisi degisebilir (trim / kolon kotu /
// profil atama). Magaza her yazmayi AYRI ve IMMUTABLE bir SURUM olarak saklar:
//
//   EntityId ──> Slot { versions:[ (VersionId, shape), ... ], active:VersionId }
//
//   * Her `set` YENI bir VersionId uretir ve onu ETKIN yapar.
//   * ESKI SURUMLER KORUNUR — yeni surum olusturmak oncekileri DEGISTIRMEZ.
//     (Boylece ileride undo, "ayni kimlik, eski geometri"yi geri yukleyebilir;
//      yalnizca kimligi geri yuklemek geometri degisikligi icin YETMEZ.)
//   * `find` / `lastLive` ETKIN surumu verir — Phase 3.1 cagiranlari icin
//     davranis AYNEN korunur.
//   * `activate(id, version)` belirli bir surumu etkinlestirir; gecersiz
//     surum/Id GUVENLI REDDEDILIR (durum degismez).
//
// IMMUTABILITE KURALI (durust sinirlama): saklanan geometri YERINDE
// DEGISTIRILMEZ. `set` cagirana verilen seklin bir HANDLE'ini saklar
// (TopoDS_Shape kopyasi sigdir — alttaki TShape paylasilir). Bu guvenlidir
// cunku bu kod tabanindaki TUM ureticiler (occ_geometry Boolean/extrude/trim,
// kolon re-extrude) YENI bir sekil DONDURUR; hicbir yol mevcut bir seklin
// TShape'ini yerinde degistirmez. Yeni surum, onceki surumun TShape'ine
// DOKUNMAZ; bu yuzden eski surumun hacmi/bbox'i degismez. Fazladan deep-copy
// (BRepBuilderAPI_Copy) BILEREK YAPILMAZ — gereksiz ve pahali olurdu.

#include "model_maker/occ_store.hpp"

#include <TopoDS_Shape.hxx>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace mm {

class OccSolidStore final : public OccStore {
public:
    // --- yazma ------------------------------------------------------------
    // YENI bir surum olusturur ve onu ETKIN yapar; uretilen VersionId'yi doner.
    // Ayni EntityId'ye ikinci yazma yeni bir surum ekler; ESKI SURUM KORUNUR.
    // Surum kimligi TUKENDIYSE (bkz. entity_id.hpp) kInvalidVersionId doner ve
    // magaza DEGISMEZ — ayni id ikinci kez URETILMEZ.
    VersionId set(EntityId id, const TopoDS_Shape& shape);

    // Belirli bir surumu ETKIN yapar. Surum (veya varlik) yoksa false doner ve
    // HICBIR SEY DEGISMEZ (guvenli reddetme).
    bool activate(EntityId id, VersionId version) noexcept;

    // --- okuma ------------------------------------------------------------
    // ETKIN surumun sekli (Phase 3.1 davranisi). Kayit yoksa nullptr.
    // (find: operator[] DEGIL — null sekil EKLEMEZ.)
    const TopoDS_Shape* find(EntityId id) const;

    // BELIRLI surumun sekli. Gecersiz VersionId / yok → nullptr.
    const TopoDS_Shape* findVersion(EntityId id, VersionId version) const;

    // Etkin surum kimligi; varlik/surum yoksa kInvalidVersionId.
    VersionId activeVersion(EntityId id) const noexcept override;

    // Undo/redo dogrulama: verilen surum bu varlik icin MEVCUT MU?
    // kInvalidVersionId ("geometri yok") GECERLI sayilir.
    bool findVersionExists(EntityId id, VersionId version) const override;

    // Undo/redo uygulama: verilen surumu ETKIN yapar. kInvalidVersionId =
    // "etkin geometri YOK" (GECERLI; `activate`den FARKLI). Gercek surum
    // yoksa false doner, hicbir sey degismez.
    bool restoreActive(EntityId id, VersionId version) override;

    // Bir varligin SAKLANAN surum sayisi (korunan eski surumler dahil).
    std::size_t versionCount(EntityId id) const noexcept;

    // Bir varligin surum kimlikleri, olusturma sirasiyla (teshis/test).
    std::vector<VersionId> versionIds(EntityId id) const;

    // Kimlik (olusturma) sirasina gore en son `count`, `isLive` gecen
    // varliklarin ETKIN sekilleri. Silinmis varliklar cagiranin filtresiyle
    // ATLANIR. (Phase 3.1 davranisi korunur.)
    std::vector<TopoDS_Shape> lastLive(std::size_t count,
        const std::function<bool(EntityId)>& isLive) const;

    void clear() noexcept override;

    // VARLIK (slot) sayisi — Phase 3.1 sozlesmesi korunur.
    std::size_t size() const noexcept override;

private:
    struct Version {
        VersionId id;
        TopoDS_Shape shape;  // immutable snapshot (handle)
    };
    struct Slot {
        std::vector<Version> versions;  // olusturma sirasi
        VersionId active{kInvalidVersionId};
    };

    // Slot icinde verilen surumu arar (yoksa nullptr).
    static const TopoDS_Shape* shapeOfVersion(const Slot& slot, VersionId version) noexcept;

    // std::map = kimlige gore sirali; kimlikler monoton artan oldugundan
    // "en son N canli kati" sondan alinabilir (lastLive).
    std::map<EntityId, Slot> slots_;

    // Monoton surum sayaci. clear() bunu SIFIRLAMAZ: surum kimlikleri asla
    // tekrar KULLANILMAZ (budama/yeniden yukleme sonrasi yanlis eslesme olmaz).
    // Sayac kInvalidVersionId'ye (0) ulasirsa id'ler TUKENMISTIR; `set` bundan
    // sonra guvenli reddeder (yeni id uretmez, ayni id'yi tekrar kullanmaz).
    VersionId nextVersionId_{1};
};

// `store` gercek OCCT magazasi ise somut API'yi verir, degilse nullptr
// (or. OCC-kapali no-op store). Tek donusum noktasi — ikinci bir harita yok.
OccSolidStore* asSolidStore(OccStore* store) noexcept;
const OccSolidStore* asSolidStore(const OccStore* store) noexcept;

// Gercek OCCT magazasini uretir (model_maker_occ). Application uygulama
// baslangicinda Document'a ENJEKTE eder.
std::unique_ptr<OccStore> makeOccSolidStore();

}  // namespace mm
