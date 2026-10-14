#pragma once

// COMPONENT FOUNDATION (P4) — parametrik bilesen altyapisi.
//
// - Main/Secondary Part rolleri (ComponentMember.role; parcalar EntityId ile
//   referanslanir — kimlik korunur).
// - Parametrik Component TANIMI (ComponentDefinition: isim + parametre semasi).
// - Instance + parametre DEGERLERI + yerlesim (ComponentInstance).
// - Bagimlilik + yeniden hesaplama: her instance TANIMA (isimle) bagimlidir;
//   tanimin parametre semasi degisince o tanimin tum instance'lari KIRLENIR
//   (dirty). recomputeDirty() degerleri semayla yeniden hizalar (eksikler
//   varsayilanla doldurulur, bilinmeyenler atilir) ve kirlenmeyi temizler.
//
// Saf veri modeli + bagimlilik (windows.h yok) — Linux'ta derlenir/test edilir.
// Gercek geometri yeniden uretimi (parametre -> B-Rep) OCC katmaninda cagrilir.

#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace mm {

enum class PartRole { None, Main, Secondary };

struct ComponentParameter {
    std::wstring name;
    double value{0.0};
    bool operator==(const ComponentParameter&) const = default;
};

struct ComponentMember {
    PartRole role{PartRole::Secondary};
    EntityId partId{kInvalidEntityId};
    bool operator==(const ComponentMember&) const = default;
};

struct ComponentDefinition {
    std::wstring name;
    std::vector<std::wstring> parameters; // parametre semasi (sira onemli)
};

struct ComponentInstance {
    std::wstring definition;                 // bagli tanim (isim)
    std::vector<ComponentParameter> values;  // parametre adi -> deger
    Vec3 origin{};                           // yerlesim
    std::vector<ComponentMember> members;    // main + secondary parcalar
    bool dirty{true};                        // yeniden hesaplama gerekli mi
    bool orphan{false};                      // tanim bulunamadi (bagimlilik kopuk)
};

class ComponentModel {
public:
    // --- Tanimlar ---
    // Ayni isimli tanim varsa REDDEDER (false); yoksa ekler (true).
    bool addDefinition(ComponentDefinition definition) noexcept;
    const ComponentDefinition* findDefinition(const std::wstring& name) const noexcept;
    bool removeDefinition(const std::wstring& name) noexcept;
    // Tanimin parametre semasini degistirir; o tanima bagli TUM instance'lari
    // kirletir (dirty=true). Tanim yoksa false.
    bool setDefinitionParameters(const std::wstring& name,
                                 std::vector<std::wstring> parameters) noexcept;
    std::size_t definitionCount() const noexcept { return definitions_.size(); }

    // --- Instance'lar ---
    // Yeni instance ekler (tanim var olmali; yoksa yine eklenir ama orphan=true).
    // Parametre degerleri semayla HIZALANIR (eksikler 0, bilinmeyenler atilir).
    // Eklenen instance'in indeksini dondurur.
    std::size_t addInstance(const std::wstring& definition,
                            std::vector<ComponentParameter> values, Vec3 origin,
                            std::vector<ComponentMember> members) noexcept;
    // Parametre degerini gunceller (semada olmayan ad reddedilir) + kirletir.
    bool setInstanceParameter(std::size_t index, const std::wstring& name, double value) noexcept;
    // Yerlesimi gunceller + kirletir.
    bool setInstanceOrigin(std::size_t index, Vec3 origin) noexcept;
    const ComponentInstance& instance(std::size_t index) const noexcept { return instances_[index]; }
    std::size_t instanceCount() const noexcept { return instances_.size(); }
    // Main part: Main rolundeki uye indeksi; yoksa nullopt.
    std::optional<std::size_t> mainPartIndex(std::size_t instanceIndex) const noexcept;

    // --- Bagimlilik + yeniden hesaplama ---
    // Kirlenmis instance'larin indeksleri.
    std::vector<std::size_t> dirtyInstances() const noexcept;
    // Tum kirli instance'lari yeniden hesaplar: tanim cikarilmis mi (orphan),
    // parametre degerlerini semayla hizalar. Yeniden hesaplanan sayiyi dondurur.
    std::size_t recomputeDirty() noexcept;

private:
    std::vector<ComponentDefinition> definitions_;
    std::vector<ComponentInstance> instances_;
    // Instance'i tanima gore yeniden hizalar; dirty=false, orphan guncellenir.
    void realignInstance(ComponentInstance& instance) noexcept;
};

} // namespace mm
