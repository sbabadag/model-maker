#pragma once

// GERCEK OCCT B-Rep magazasi.
//
// YALNIZCA OCC-acik derleme birimleri (src/occ_solid_store.cpp, application.cpp
// MM_HAS_OCC dallari, OCC harness) bu basligi include eder; OCCT basliklari
// gerektirir. Document bu tipi GORMEZ — yalnizca abstract OccStore& uzerinden
// sahiplenir. Somut API'ye Application `asSolidStore()` ile erisir.

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
    // KIMLIK -> sekil. `set` ayni kimlige yazarsa onu GUNCELLER (trim/kolon
    // kotu gibi yerinde degistirmeler). Kimlik URETMEZ — kimlik yasam dongusu
    // Document'tadir.
    void set(EntityId id, const TopoDS_Shape& shape);

    // Kayit yoksa nullptr. (find: operator[] DEGIL — null sekil EKLEMEZ.)
    const TopoDS_Shape* find(EntityId id) const;

    // Kimlik (olusturma) sirasina gore en son `count`, `isLive` gecen sekiller.
    // Silinmis varliklarin kayitlari cagiranin filtresiyle ATLANIR.
    std::vector<TopoDS_Shape> lastLive(std::size_t count,
        const std::function<bool(EntityId)>& isLive) const;

    void clear() noexcept override;
    std::size_t size() const noexcept override;

private:
    // std::map = kimlige gore sirali; kimlikler monoton artan oldugundan
    // "en son N canli kati" sondan alinabilir (lastLive).
    std::map<EntityId, TopoDS_Shape> shapes_;
};

// `store` gercek OCCT magazasi ise somut API'yi verir, degilse nullptr
// (or. OCC-kapali no-op store). Tek donusum noktasi — ikinci bir harita yok.
OccSolidStore* asSolidStore(OccStore* store) noexcept;
const OccSolidStore* asSolidStore(const OccStore* store) noexcept;

// Gercek OCCT magazasini uretir (model_maker_occ). Application uygulama
// baslangicinda Document'a ENJEKTE eder.
std::unique_ptr<OccStore> makeOccSolidStore();

}  // namespace mm
