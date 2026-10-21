#pragma once
// KULLANICI COMPONENT KATALOGU (tasinabilir, kalici derived sablonlar).
//
// Amac: Tekla'dan bridge ile cekilen bir baglantiyi Model Maker'in KENDI
// native component kataloguna SABLON olarak kaydetmek. Tekla yalnizca ornek/
// sablon kaynagidir; kaydedilen sablon Tekla kapaliyken de ribbondan secilip
// yeni projede parametrik + reusable component uretir.
//
// MIMARI (genisletilebilir, Tekla numarasina ozel kod TEKRARI YOK):
//   * Bir derived sablon = kaynak (family) ComponentType'in parametre semasi +
//     generator fonksiyonu + SABLONA OZEL varsayilan degerler + benzersiz
//     typeId. Generator AILESI kaynaktan (sourceTypeId) miras alinir; yeni
//     bir Tekla numarasi icin yeni kod degil, yeni bir aile (generator modulu)
//     gerekir. Ileride stiffener/haunch/angle/notch/weld ayri generator
//     modulleri olarak eklenir; her modul kendi family id'siyle kayitlanir.
//   * Sablon typeId'si OLUSTURULUNCA SABITLENIR (katalog dosyasinda saklanir,
//     yuklemede yeniden turetilmez). Katalog degisikligi mevcut projelerdeki
//     instance'lari etkilemez: instance kendi parametrelerini MMW8'de tasir.
//
// TAŞINABILIRLIK:
//   * Katalog JSON dosyasidir (nlohmann/json); farkli bilgisayara kopyalanir.
//   * Bir proje kayitli typeId'yi katalogda bulamazsa instance ORPHAN kalir
//     (geometry korunur, recompute edilmez) ve acik uyari verilir.
//
// DURUSTLUK:
//   * Unsupported geometri iceren baglanti SESSIZCE eksik native component
//     olarak kaydedilmez; yalnizca isReferenceOnly=true ile acikca etiketlenmis
//     REFERANS sablonu olarak kaydedilmesine izin verilir (cizim/parametre
//     kaydi; "calisir parametrik component" iddiasi yapilmaz).
//
// OCCT'sizdir; Linux OCC-off ctest'te kosar. Bagimlilik: component_engine
// (saf veri modeli) + nlohmann/json.

#include "model_maker/component_engine.hpp"
#include "model_maker/component_dialog.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace mm {

// Kullanici katalogundaki TEK derived sablon.
struct UserCatalogEntry {
    std::wstring typeId;              // benzersiz + DEGISMEYEN (katalogda saklanir)
    std::wstring displayName;         // kullanicinin verdigi ad
    std::wstring sourceTypeId;        // family generator kaynagi (orn "tekla_shear_plate_146")
    std::wstring generatorFamily;     // aile etiketi (ileride moduler generator'lar icin)
    std::wstring sourceComponentNumber;  // Tekla/standart no ("146"); bos olabilir
    int schemaVersion{1};

    // SABLONUN TAM parametre semasi (family semasinin kopyasi; varsayilanlar
    // sablona ozel degerlere cevrilmis). Dialog + dogrulama + uretim bunu
    // kullanir — family daha sonra degisse bile sablon kendi semasini korur.
    std::vector<CompParamSpec> specs;

    // Katalog metadatasi (family'dan kopyalanir, sablonda sabitlenir).
    std::wstring category;
    std::wstring primaryMemberType;
    std::wstring secondaryMemberType;
    std::vector<std::wstring> supportedFeatures;
    std::wstring coverageStatus;
    int pickCount{2};

    // Unsupported geometri => yalnizca REFERANS sablonu (parametrik DEGIL).
    bool isReferenceOnly{false};
    std::wstring referenceNote;
};

// Bir sablonu ComponentType'a cevirir (catalogVisible=true; registry'ye bu
// haliyle kaydedilir). typeId/displayName/specs/catalog meta sablondan gelir.
ComponentType userCatalogEntryToComponentType(const UserCatalogEntry& e) noexcept;

// Kaynak family tipi + sablon adi + varsayilan override'larindan yeni bir
// sablon turetir. typeId benzersiz ve belirlenimci uretilir (mevcut
// typeId'lerle cakisirsa son eke _2/_3 ekler). sourceTypeId'si kaynagin
// typeId'si olur; generatorFamily ayni degerle baslar (cagiran ezebilir).
UserCatalogEntry deriveComponentTemplate(const ComponentType& source,
                                         const std::wstring& displayName,
                                         const std::vector<CompParam>& defaultOverrides,
                                         bool isReferenceOnly = false,
                                         const std::wstring& referenceNote = L"");

// Kalici kullanici katalogu (JSON dosya). Yalnizca veri + kayit mantigi tasir;
// registry/dialog kaydini registerInto yapar (cagiran hangi registry'leri
// besleyecegini bilir).
class UserCatalog {
public:
    UserCatalog() = default;

    // JSON dosyasini yukler; basarisiz/eksik dosya => bos katalog (hata firlatmaz).
    // ok=false ise parse hatasi; error insan okunur mesaj.
    bool load(const std::filesystem::path& path, std::string& error) noexcept;
    // Katalogu JSON dosyasina yazar (atomik olmayan, dogrudan yazim). ok=false
    // ise error dolu.
    bool save(const std::filesystem::path& path, std::string& error) const noexcept;

    const std::vector<UserCatalogEntry>& entries() const noexcept { return entries_; }
    const UserCatalogEntry* find(const std::wstring& typeId) const noexcept;

    // Sablon ekler veya (typeId eslesiyorsa) gunceller. true = eklendi,
    // false = guncellendi.
    bool addOrUpdate(const UserCatalogEntry& e) noexcept;
    bool remove(const std::wstring& typeId) noexcept;
    std::size_t size() const noexcept { return entries_.size(); }

    // e.typeId baska bir (FARKLI adli) sablonla cakisiyorsa _2/_3 son ekini
    // ekleyerek benzersizlestirir. Ayni ad => ayni typeId (guncelleme) korunur.
    void ensureUnique(UserCatalogEntry& e) const noexcept;

    // Sablonlari ComponentRegistry + ComponentDialogRegistry'ye kaydeder.
    // Her sablon generatorunu sourceTypeId (family) kaydindan miras alir;
    // family kayitli degilse o sablon ATLANIR ve warning listesine yazilir
    // (instance orphan kalmamasi icin; geometri kaybolmaz).
    // Donus: kayit edilemeyen sablonlar icin acik uyarilar (bos = hepsi tamam).
    std::vector<std::wstring> registerInto(ComponentRegistry& reg,
                                           ComponentDialogRegistry& dialogs) const noexcept;

    // Katalogda verilen typeId'lerin hangilerinin KAYITLI olmadigini dondurur
    // (acilista MMW8 instance'lari icin orphan teshisi).
    std::vector<std::wstring> missingTypeIds(const std::vector<std::wstring>& typeIds) const noexcept;

private:
    std::vector<UserCatalogEntry> entries_;
};

} // namespace mm
