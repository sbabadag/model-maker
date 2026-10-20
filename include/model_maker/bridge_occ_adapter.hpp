#pragma once
// MODEL MAKER BRIDGE — OCC-on katman (Tekla -> Model Maker gercek kati).
//
// V2 paketindeki dogrulanmis plaka bbox'i ve bulon konumlarindan GERCEK OCCT
// kati uretir, bulon deliklerini KESER ve Document'a ATOMIK olarak isler.
//
// DURUSTLUK:
//   * Plaka, dogrulanmis bbox'tan eksen-hizali kutu olarak YENIDEN URETILIR
//     (kaynak kati birebir kopyalanmaz — bbox disindaki topoloji YOK).
//   * Bulon ekseni + delik capi KARARLARI BELGELENIR (measured/inferred/standard);
//     dogrulanamayan delik KESILMEZ, acik diagnostic raporlanir.
//   * Import edilen component typeId "tekla_bridge_import" registry'de YOKTUR
//     (orphan) -> parametrik/editable DEGILDIR; yeniden olusturulmus geometridir.
//   * Uye referanslari (primary/secondary) + kaynak GUID'leri `_bridge_*` string
//     parametreleri olarak saklanir (kaynak referansi, EntityId DEGIL).
//
// Yalnizca MM_HAS_OCC derlemelerinde derlenir (Windows uygulamasi + Linux OCC
// test harness'i). OCC-kapali ctest bu modulu GORMEZ.

#ifdef MM_HAS_OCC

#include "model_maker/bridge_package.hpp"
#include "model_maker/document.hpp"

#include <TopoDS_Shape.hxx>

namespace mm {

// --- OCC-on PoC: plaka kati + bulon delikleri ---------------------------------

// Eksen-hizali dikdortgen plaka kati (bbox'tan). Dejenere (sifir boyut) bbox
// icin IsNull doner.
TopoDS_Shape buildBridgePlateSolid(const BridgeBBox& bbox);

// Bulon ekseni + delik capi KARARI (belgelenmis; uydurulmaz).
struct BridgeBoltPlan {
    bool ok = false;
    std::string error;
    Vec3 axis{1.0, 0.0, 0.0};      // birim eksen
    std::string axisSource;        // "measured" | "inferred"
    double holeDiameter = 0.0;     // mm
    std::string diameterSource;    // "standard" | "measured" | "inferred"
    std::vector<Vec3> holeOrigins; // bulon basina delik ekseni merkez noktasi
    std::vector<bool> boltInside;  // holeOrigins ile hizali: plaka ayak izinde mi
    std::vector<std::string> diagnostics; // bulon basi uyari (bos = ok)
};

// Bulon grubunu plaka bbox'ina gore planlar. Eksen: info.axis ("x"->+X, ...);
// bos ise plakanin EN INCE boyutundan cikarilir (inferred). Cap: bolt.holeDiameter
// > 0 ise o (kaynak), degilse nominal cap -> standardBoltHoleDiameter (M20->22).
// Her bulonun eksene dik duzlemdeki koordinati plaka ayak izinde mi dogrulanir;
// disinda kalanlar boltInside=false + diagnostic (delik KESILMEZ).
BridgeBoltPlan planBridgeBolts(const BridgeBoltGroupInfo& info,
                               const BridgeBBox& plateBBox);

struct BridgeHoleCutResult {
    bool ok = false;
    std::string error;
    TopoDS_Shape solid;          // delikler kesilmis kati (IsNull = hata)
    int holesCut = 0;
    int holesSkipped = 0;
    std::vector<std::string> diagnostics;
};

// Yalniz dogrulanmis (boltInside=true) delikleri keser. Delik = eksen boyunca
// through-cut silindir (cap = holeDiameter). Plaka disina dusen/boolean basarisiz
// delikler SKIP edilir ve acik diagnostic ile raporlanir (sessiz basari YOK).
BridgeHoleCutResult cutBridgeBoltHoles(const TopoDS_Shape& plateSolid,
                                       const BridgeBoltPlan& plan);

// --- Document entegrasyonu (atomik) -------------------------------------------

struct BridgeImportOutcome {
    bool ok = false;
    std::string error;
    EntityId plateId{kInvalidEntityId};
    EntityId boltGroupId{kInvalidEntityId};
    EntityId componentId{kInvalidEntityId};
    int holesCut = 0;
    int holesSkipped = 0;
    std::vector<std::string> diagnostics;
    std::vector<std::string> warnings;
};

// V2 paketini Document'a ATOMIK olarak isler (pushSnapshot + hata rollback).
// Uretilenler: plaka (kati + wireframe + delikler), bulon grubu (display + delik
// modifier'lari), component instance (typeId "tekla_bridge_import", orphan =
// parametrik DEGIL). Basarisizlikta belge DEGISMEZ (rollbackOpenRecord).
// Undo/redo tek adimda geri alir.
BridgeImportOutcome importBridgeComponent(Document& doc, const BridgePackage& pkg);

}  // namespace mm

#endif  // MM_HAS_OCC
