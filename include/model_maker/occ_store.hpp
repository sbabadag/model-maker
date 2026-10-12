#pragma once

// OPAK B-Rep magaza arayuzu (OCCT-SIZ).
//
// Document, B-Rep sekillerini bu arayuz uzerinden SAHIPLENIR. Bu baslik HICBIR
// OCCT basligi icermez; sozlesmesi (imzalar) yalnizca EntityId ve standart
// tipler kullanir. Boylece:
//   * document.hpp OCCT'ye bagimli OLMAZ (OCC-kapali derleme korunur),
//   * TopoDS_Shape genis include edilen basliklara SIZMAZ,
//   * somut OCCT uygulamasi (bkz. occ_solid_store.hpp) yalnizca OCC-acik
//     derleme birimlerinde ORTAYA CIKAR ve Document'a ENJEKTE edilir.
//
// Saglayici secimi LINK SIRASINA bagli DEGILDIR (weak symbol YOK):
//   * makeNullOccStore()  -> model_maker_core (her zaman link; no-op tablo).
//     Document varsayilan olarak bununla kurulur.
//   * makeOccSolidStore() -> model_maker_occ (MM_HAS_OCC). Application,
//     OCC-acik derlemede bunu uretip Document'a enjekte eder.

#include "model_maker/entity_id.hpp"

#include <cstddef>
#include <memory>

namespace mm {

class OccStore {
public:
    virtual ~OccStore() = default;

    // Tum B-Rep sekillerini birakir (belge temizleme/yukleme).
    virtual void clear() noexcept = 0;

    // Tutulan sekil (kayit) sayisi.
    virtual std::size_t size() const noexcept = 0;

    bool empty() const noexcept { return size() == 0; }
};

// OCC-kapali no-op saglayici (model_maker_core).
std::unique_ptr<OccStore> makeNullOccStore();

}  // namespace mm
