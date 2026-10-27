#pragma once
// COMPONENT AYAR SETLERI (Tekla "Save / Save as / Load" — adlandirilmis parametre setleri).
//
// Bir bilesenin diyalogundaki PARAMETRE DEGERLERI bir adla saklanir ve sonra geri
// yuklenir. Secili kolon/kiris, kesit veya model nesnesi SAKLANMAZ — yalnizca
// parametre degerleri. Aileden bagimsizdir: anahtar bilesen typeId'sidir, bu yuzden
// tek mekanizma tum diyalog tabanli bilesenlerde (Haunch 40, 146, fin plate, ...) calisir.
//
//  * "standard" ozel adlidir: her bilesen icin HER ZAMAN listelenir. Kayitli bir
//    "standard" yoksa bilesenin sema varsayilanlari anlamina gelir; kaydedilirse
//    menuden yeni yerlestirilen bilesenler bu degerlerle baslar.
//  * Set diskte JSON'dur (kullanici katalogu gibi exe'nin yaninda); farkli bilgisayara
//    kopyalanir. Bozuk/eksik dosya => bos depo (hata firlatmaz, ok=false + mesaj).
//  * OCCT'sizdir, Qt'siz; Linux ctest'te kosar.

#include "model_maker/component_engine.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace mm {

inline const wchar_t* const kStandardPresetName = L"standard";

struct ComponentPreset {
    std::wstring typeId;
    std::wstring name;
    std::vector<CompParam> params;
};

class ComponentPresetStore {
public:
    // Dosyadan yukler. Dosya yok/bos => bos depo, true. Parse hatasi => false + error.
    bool load(const std::filesystem::path& path, std::string& error) noexcept;
    // Once gecici dosyaya yazar, sonra yeniden adlandirir (yari yazilmis dosya kalmaz).
    bool save(const std::filesystem::path& path, std::string& error) const noexcept;

    // Bilesenin set adlari: "standard" HER ZAMAN ilk, digerleri alfabetik.
    std::vector<std::wstring> names(const std::wstring& typeId) const;
    // Kayitli set; "standard" kayitli degilse nullptr (cagiran sema varsayilanini kullanir).
    const ComponentPreset* find(const std::wstring& typeId, const std::wstring& name) const noexcept;
    bool contains(const std::wstring& typeId, const std::wstring& name) const noexcept {
        return find(typeId, name) != nullptr;
    }
    // Ekler veya (ayni typeId+ad) uzerine yazar. true = yeni eklendi, false = guncellendi.
    // Gecersiz ad (bos / yalniz bosluk / kontrol karakteri / >64) => false ve DEGISMEZ;
    // cagiran once validName() ile sorar.
    bool put(const std::wstring& typeId, const std::wstring& name,
             const std::vector<CompParam>& params);
    bool remove(const std::wstring& typeId, const std::wstring& name);
    std::size_t size() const noexcept { return presets_.size(); }

    // Bas/son bosluk kirpilmis hali gecerli mi? (bos degil, <=64, kontrol karakteri yok).
    static bool validName(const std::wstring& name) noexcept;
    static std::wstring trimmed(const std::wstring& name);

private:
    std::vector<ComponentPreset> presets_;
};

} // namespace mm
