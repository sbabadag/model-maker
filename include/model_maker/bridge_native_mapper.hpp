#pragma once
// MODEL MAKER BRIDGE — Tekla paketini NATIVE parametrik component'e esleyen
// katman (Bridge V3 -> native "tekla_shear_plate_146").
//
// Amac: `tekla_bridge_import` orphan component (yalniz donmus geometri) yerine,
// kaynak Tekla 146 "Shear plate simple" baglantisinin GERCEK parametrelerini
// cikarip native generator'un parametre semasina tasimak. Boylece import edilen
// baglanti DUZENLENEBILIR (parametrik) hale gelir; native generator desteklemedigi
// ozelligi SESSIZCE ATLAMAZ, acikca "unsupported" raporlar.
//
// DURUSTLUK KURALLARI:
//   * Her cikarilan parametre bir KAYNAK tasir (Observed/Documented/Inferred/
//     Unresolved). Belirsiz deger KESIN gibi isaretlenmez.
//   * Native generator'un yeniden uretemedigi geometri (cok kolonlu bulon grubu,
//     kaynak geometrisi, fitting/cut cesitleri, cift tab desteklenmeyen yanlari)
//     "unsupportedFeatures" listesine yazilir; veri KAYBEDILMEZ.
//   * Esdegerlik (equivalence) Uc durumludur: Equivalent / NotEquivalent /
//     Unverified. Uye geometrisi pakette yoksa generator CALISTIRILMAZ (uydurulmaz);
//     sonuc Unverified olur.
//
// OCCT'sizdir; Linux OCC-off ctest'te kosar. Yalnizca component_engine (saf veri
// modeli) + profile_database + bridge_package bagimliliklari vardir.

#include "model_maker/bridge_package.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/profile_database.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mm {

// Bir parametrenin kaynagi (provenance).
enum class BridgeParamProvenance {
    Observed,     // dogrudan kaynak veriden olculdu (parametre/bbox/bulon konumu)
    Documented,   // Tekla dokumantasyonundan bilinen; kaynakta dogrulanamadi
    Inferred,     // kaynaktan turetildi (standard delik capi, eksen, varsayilan)
    Unresolved    // belirlenemedi; uydurulmadi
};

const char* bridgeProvenanceName(BridgeParamProvenance p) noexcept;

// BridgeParameter.confidence -> BridgeParamProvenance.
BridgeParamProvenance bridgeConfidenceToProvenance(const std::string& confidence) noexcept;

// Native parametre + kaynagi.
struct BridgeMappedParam {
    std::wstring name;                 // native CompParam adi (semaya gore)
    CompParamValue value;              // tipli deger
    BridgeParamProvenance provenance;  // kaynak
    std::string note;                  // kaynagin kisa aciklamasi
};

// Esdegerlik durumu.
enum class BridgeEquivalenceState {
    Equivalent,     // native generator gozlenen geometriyi yeniden uretiyor
    NotEquivalent,  // yeniden uretemiyor (sebep issues'da)
    Unverified      // uye geometrisi eksik; generator calistirilamadi (uydurulmadi)
};

const char* bridgeEquivalenceName(BridgeEquivalenceState s) noexcept;

struct BridgeEquivalence {
    BridgeEquivalenceState state{BridgeEquivalenceState::Unverified};
    std::vector<std::string> issues;  // NotEquivalent ise nedenler
};

// Yeniden olusturulmus uye (profil katalogundan + paketten).
struct BridgeReconstructedMember {
    MemberGeometry geometry;         // axisFrom/axisTo/secWidth/secHeight/...
    std::string profile;             // "HEA400", ...
    bool hasAxis{false};             // startPoint/endPoint vardi mi
    BridgeParamProvenance axisProvenance{BridgeParamProvenance::Unresolved};
    bool hasSection{false};          // profil katalogunda bulundu mu
};

// Bridge -> native esleme sonucu.
struct BridgeNativeMapping {
    bool detected{false};                 // kaynak 146 shear plate mi
    std::string detectionReason;          // neden (eslesme kurali)
    std::wstring typeId;                  // L"tekla_shear_plate_146" (detected ise)

    std::vector<BridgeMappedParam> params;  // native parametreler (provenance ile)

    // Kaynakta var ama native generator'da karsiligi olmayanlar (acikca raporlanir).
    std::vector<std::string> unsupportedFeatures;

    // Gozlenen ama native semada yeri olmayan metadata (part ozellikleri).
    std::string plateMaterial;              // gozlenen malzeme (S235JR ...)
    BridgeParamProvenance materialProvenance{BridgeParamProvenance::Unresolved};

    BridgeReconstructedMember primary;      // kolon
    std::vector<BridgeReconstructedMember> secondary;  // kiris(ler)

    BridgeEquivalence equivalence;
};

// Paketi native component'e esler. Kaynak 146 degilse detected=false ve params
// bos doner (yine de ok=false DEGILDIR — bu bir esleme sonucudur).
// catalog: profil katalogu (kesit boyutlari icin). Bos olabilir; o zaman uye
// kesit boyutlari cikarilamaz ve equivalence Unverified olur (parametre
// cikarimi ETKILENMEZ — o katalog gerektirmez).
BridgeNativeMapping mapBridgeToNative(const BridgePackage& pkg,
                                      const std::vector<SteelProfile>& catalog) noexcept;

} // namespace mm
