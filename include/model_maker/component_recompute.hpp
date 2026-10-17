#pragma once

// COMPONENT DEPENDENCY + RECOMPUTE (Phase R2d-2) — Tekla tarzi parametrik
// component recompute altyapisi. Bir parca (kolon/kiris) degistiginde ona
// bagli component'ler belirlenir, deterministik sirayla yeniden cozulur ve
// sonuc ATOMIK (ya hep ya hic) uygulanmak uzere PLAN olarak dondurulur.
//
// Bu katman OCCT'den BAGIMSIZ ve MUTASYON YAPMAZ: yalnizca (1) bagimlilik
// kesfi (parca -> component ters indeksi), (2) deterministik sira, (3) cozum
// plani (durum + yeni yerlesim) uretir. Gercek geometri (levha/bulon/delik)
// uretimi ve uygulama Document/OCC katmaninda yapilir — plan basarisizsa
// cagiran ESKI gecerli geometriyi KORUR (mevcut saglam geometri DEGISMEZ).

#include "model_maker/component_engine.hpp"

#include <functional>
#include <map>
#include <set>
#include <vector>

namespace mm {

// Parca EntityId -> o parcaya bagli component EntityId'leri (ters indeks).
// Deterministik (artan id); ayni kolondaki birden fazla component bagimsiz ve
// sirali cozulur. Yalnizca main/secondary referanslarini izler (ComponentPlane
// gibi component-arası baglar ileriki dilimde eklenir).
class ComponentDependencyGraph {
public:
    // components listesinden ters indeksi kurar (main + secondary referanslari).
    void build(const std::vector<ComponentInstanceRecord>& components);
    // Verilen parcanin bagli component'leri (artan id).
    std::vector<EntityId> dependents(EntityId partId) const;
    // Verilen parcalardan HERHANGI birine bagli component'ler (tekillestirilmis,
    // artan id).
    std::vector<EntityId> dependentsOf(const std::vector<EntityId>& parts) const;
    // Bir component verilen parcaya bagli mi (main veya secondary)?
    static bool dependsOn(const ComponentInstanceRecord& record, EntityId partId);
    std::size_t componentCount() const noexcept { return componentCount_; }

private:
    std::map<EntityId, std::set<EntityId>> partToComponents_;
    std::size_t componentCount_{0};
};

// Cozum durumu (kullanici kabulu: basari, unresolved, conflict, cyclic, geometry).
enum class RecomputeStatus {
    Success,
    Unresolved,       // referans/geometri eksik
    Conflict,         // tutarsiz kisit
    Cyclic,           // dongusel bagimlilik
    GeometryFailure,  // geometri uretilemedi
};

// Bir component'in yeniden cozum ciktisi.
struct RecomputeItem {
    EntityId componentId{kInvalidEntityId};
    RecomputeStatus status{RecomputeStatus::Unresolved};
    Vec3 origin{};            // yeni yerlesim (Success'te gecerli)
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};
    Vec3 axis{0.0, 0.0, 1.0};
    std::wstring error;
};

// Recompute ORKESTRATORU (mutasyon yok). Degisen parcalardan bagli component'leri
// kesfeder, deterministik sirayla resolver'i cagirir, per-component durum + yeni
// yerlesim plani dondurur. Resolver (gercek temas cozumu) enjekte edilir; bu
// katman yalnizca sira + toplama yapar. plan(...).allSucceeded == false ise
// cagiran HICBIR degisikligi uygulamamali (eski geometri korunur).
class RecomputePlanner {
public:
    using Resolver = std::function<RecomputeItem(const ComponentInstanceRecord&)>;

    struct Plan {
        bool allSucceeded{false};
        std::vector<RecomputeItem> items;
    };

    // changedParts: degisen parca EntityId'leri. components: TUM component
    // kayitlari (guncel). Yalnizca changedParts'e bagli component'ler cozulur;
    // digerleri listede YOKTUR (bagimsiz kalir).
    Plan plan(const std::vector<ComponentInstanceRecord>& components,
              const std::vector<EntityId>& changedParts,
              const Resolver& resolver) const;
};

} // namespace mm
