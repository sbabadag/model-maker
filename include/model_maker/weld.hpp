#pragma once

// WELD + ASSEMBLY (imalat) veri modeli — EntityId-tabanli kalici varliklar.
//
// Tekla benzeri iki kavram:
//   - Weld: iki Part (mainPart + secondaryPart) arasinda atolye/santiye kaynagi.
//     Bagimsiz EntityId'si olan GERCEK bir model varligidir (yalniz metadata degil).
//     Shop weld iki parçayi AYNI imalat Assembly'sine sokar; Site weld iki farkli
//     Assembly'yi BIRLESTIRMEZ (yalniz modelde iliskili baglanti olarak tutulur).
//   - AssemblyRecord: benzersiz EntityId'li imalat grubu (main part + uye parcalar).
//     Uyelik, shop-weld grafinden TURETILIR (deterministik) — kullanici EntityId'leri
//     ve main part kimligi korunur.
//
// Windows yok; Linux'ta derlenir/test edilir. Weld OCC katisi ayri bir katmanda
// uretilir (weld_geometry.hpp); bu baslik YALNIZ veri modeli + yerlesim tasir.

#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"

#include <string>
#include <vector>

namespace mm {

// Kaynak tipi (ilk asama): koseli (fillet) ve alin (butt).
enum class WeldType { Fillet, Butt };

// Atolye / santiye kaynagi.
enum class WeldClass { Shop, Site };

// Bir kaynagin kalici kaydi. mainPart = ana parca (kullanici tarafindan
// belirlenebilir), secondaryPart = ikincil parca. Her ikisi de MEWCUT EntityId
// olarak tutulur (baglanti silinse de parcalar KORUNUR).
struct Weld {
    EntityId id{kInvalidEntityId};
    EntityId mainPart{kInvalidEntityId};
    EntityId secondaryPart{kInvalidEntityId};
    WeldType type{WeldType::Fillet};
    double size{5.0};          // mm (bkz sizeIsThroat)
    double length{0.0};        // mm (0 = otomatik/tam temas uzunlugu)
    WeldClass weldClass{WeldClass::Shop};
    // Kaynak olcusu anlami (Tekla'dan dogrulanmadan DONUSTURME): fillet icin
    // bacak (leg) ya da bogaz (throat); butt icin agiz kalinligi. Varsayilan
    // bacak (leg) — gercek tekla olcusunun anlami bilinmeden varsayilmaz.
    bool sizeIsThroat{false};  // false = bacak (leg); true = bogaz (throat)
    bool doubleSided{false};   // cift tarafli kaynak (iki ayri kati)
    // Kaynak referans geometrisi (otomatik hesaplanir; kullanici dogrulayabilir).
    Vec3 origin{};             // kaynak referans noktasi
    Vec3 direction{1.0, 0.0, 0.0}; // kaynak ilerleme yonu (birim)
    // Kaynak kesitinin iki parçaya gore yonelim normali (gercek OCC kati uretimi
    // icin; MMW8'de korunur). Gecersiz birlesimlerde sifir kalir.
    Vec3 mainNormal{};         // ana parca yuzu normali (birim)
    Vec3 secNormal{};          // ikincil parca yuzu normali (birim)
    // Ust / alt kaynak ozellikleri (Tekla Above/Below).
    bool aboveLine{true};
    // Kimlik / metadata.
    std::string identification;  // "W001"
    std::string remarks;
    // OCC katisi uretilebildi mi? Gecersiz/uyumsuz birlesimlerde sessizce yanlis
    // katı uretilmez; entity + metadata korunur, geometri acikca unsupported.
    bool geometrySupported{false};
    std::string unsupportedReason;
    // Kaynak katisinin GORUNTULEME modelinin EntityId'si (normal model; OCC B-Rep
    // bu kimlik altinda magazada tutulur). Gecersiz = kati uretilmedi/sembol.
    EntityId displayEntityId{kInvalidEntityId};

    bool operator==(const Weld&) const = default;
};

// Kalici imalat Assembly kaydi. mainPart + parts (mainPart dahil) EntityId'leri
// korunur; kimlikler silme/degistirmede DEGISMEZ.
struct AssemblyRecord {
    EntityId id{kInvalidEntityId};
    std::wstring name;           // or. "A001"
    EntityId mainPart{kInvalidEntityId};
    std::vector<EntityId> parts; // uye parcalar (mainPart dahil, tekil)
    std::string createdAt;       // olusturma bilgisi (revision no, string)
    std::string updatedAt;       // guncelleme bilgisi

    bool operator==(const AssemblyRecord&) const = default;
};

} // namespace mm
