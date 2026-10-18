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
//
// SLICE 2d-2c (generic + atomik): ComponentRefs ortak temsili ile grafik ve
// planlayici artık TEK BIR component tipine (ComponentInstanceRecord) bagli
// degildir; EndPlateConnection ve gelecekteki Base Plate/Fin Plate/Gusset/
// Splice ayni hatti kullanir. `atomicRecompute` (prepare-then-commit) ise
// coklu component yeniden hesaplamasini GERCek atomik yapar: tum yeni geometri
// ONCE gecici uretilir (prepare), yalnizca HEPSI basariliysa uygulanir
// (commit). Bir prepare bile basarisizsa HICBIR commit cagrilmaz — kismi
// belge degisikligi YOKTUR.

#include "model_maker/component_engine.hpp"
#include "model_maker/end_plate.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace mm {

// ORTAK COMPONENT REFERANS TEMSILI (generic). Bir component'in kimligi + iliskili
// parca EntityId'leri (main + secondary + digerleri). Hem ComponentInstanceRecord
// hem EndPlateConnection bu temsile uyarlanir; grafik/planlayici yalnizca bunu
// gorur — Document/Application icinde component tipine OZEL recompute yolu YOKTUR.
struct ComponentRefs {
    EntityId id{kInvalidEntityId};
    std::vector<EntityId> parts;  // main + secondary (sira onemsiz; grafik tekillestirir)
};

// Adapter: parametrik component kaydi -> ComponentRefs.
ComponentRefs componentRefs(const ComponentInstanceRecord& record) noexcept;
// Adapter: gercek End Plate baglantisi -> ComponentRefs.
ComponentRefs componentRefs(const EndPlateConnection& conn) noexcept;

// Parca EntityId -> o parcaya bagli component EntityId'leri (ters indeks).
// Deterministik (artan id); ayni kolondaki birden fazla component bagimsiz ve
// sirali cozulur. Yalnizca main/secondary referanslarini izler (ComponentPlane
// gibi component-arası baglar ileriki dilimde eklenir).
class ComponentDependencyGraph {
public:
    // components listesinden ters indeksi kurar (main + secondary referanslari).
    void build(const std::vector<ComponentInstanceRecord>& components);
    // GENERIC: ComponentRefs listesinden ters indeksi kurar (herhangi bir
    // component tipi — EndPlateConnection dahil).
    void build(const std::vector<ComponentRefs>& components);
    // Verilen parcanin bagli component'leri (artan id).
    std::vector<EntityId> dependents(EntityId partId) const;
    // Verilen parcalardan HERHANGI birine bagli component'ler (tekillestirilmis,
    // artan id).
    std::vector<EntityId> dependentsOf(const std::vector<EntityId>& parts) const;
    // Bir component verilen parcaya bagli mi (main veya secondary)?
    static bool dependsOn(const ComponentInstanceRecord& record, EntityId partId);
    static bool dependsOn(const ComponentRefs& refs, EntityId partId) noexcept;
    std::size_t componentCount() const noexcept { return componentCount_; }

private:
    void buildFromRefs(const std::vector<ComponentRefs>& refs);
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
    // GENERIC: resolver component kimligiyle cagrilir (ComponentRefs uzerinden).
    using ResolverById = std::function<RecomputeItem(EntityId componentId)>;

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

    // GENERIC: ComponentRefs + kimlik-anahtarli resolver. Herhangi bir component
    // tipi (EndPlateConnection dahil) ayni planlama hattini kullanir.
    Plan planRefs(const std::vector<ComponentRefs>& components,
                  const std::vector<EntityId>& changedParts,
                  const ResolverById& resolver) const;
};

// ATOMIK RECOMPUTE (prepare-then-commit, ya hep ya hic). `Prepared` cagiranin
// urettigi gecici sonuc turu — OCCT tipleri BU KATMANDA DEGIL, cagiran katmanda
// kalir (prepare callback'i TopoDS_Shape dondurebilir; bu baslik OCCT bilmez).
//
//   prepare(componentId) -> std::optional<Prepared>
//       Tum HATALI is (geometri uretimi, temas cozumu, dogrulama) burada yapilir.
//       nullopt = basarisizlik.
//   commit(componentId, prepared) -> bool
//       Yalnizca infallible document yazimlari (setEndPlateConnection,
//       putOccSolid, recomputeCutTarget). HATA ATMAMALI — tum fallible isler
//       prepare'da bitti. Savunma olarak false dondurebilir (commit reddedildi);
//       o zaman cagiran transaction'i rollback etmelidir (out.commitFailed).
//
// Semantik: affected ids deterministik (artan) siralanir ve tekillestirilir;
// HEPSI prepare edilir; BIRI bile basarisizsa HICBIR commit cagrilmaz (kismi
// uygulama yok). allPrepared == false ise cagiran belgeyi DEĞİŞTİRMEMİŞ olur.
template <typename Prepared>
struct AtomicRecomputeResult {
    bool allPrepared{false};
    bool commitFailed{false};          // bir commit false dondu (kismi uygulama)
    std::vector<EntityId> order;      // deterministik (artan id) islem sirasi
    std::vector<EntityId> committed;  // yalnizca hepsi basariliysa dolu
    std::vector<EntityId> failed;     // prepare'i basarisiz olanlar
    std::wstring error;
};

template <typename Prepared, typename PrepareFn, typename CommitFn>
AtomicRecomputeResult<Prepared> atomicRecompute(std::vector<EntityId> affected,
                                                PrepareFn&& prepare,
                                                CommitFn&& commit) {
    AtomicRecomputeResult<Prepared> out;
    std::sort(affected.begin(), affected.end());
    affected.erase(std::unique(affected.begin(), affected.end()), affected.end());
    out.order = affected;

    std::vector<Prepared> staged;
    staged.reserve(affected.size());
    for (EntityId id : affected) {
        auto prepared = prepare(id);
        if (!prepared) {
            out.failed.push_back(id);
            out.error = L"component recompute hazirlik hatasi (id=" +
                        std::to_wstring(static_cast<std::uint64_t>(id)) + L")";
            return out;  // HICBIR commit calismaz — kismi uygulama YOK
        }
        staged.push_back(std::move(*prepared));
    }
    out.allPrepared = true;
    for (std::size_t i = 0; i < affected.size(); ++i) {
        // commit infallible olmali ama savunma olarak false dondurebilir; o
        // durumda KISMI uygulama olusmustur — cagiran transaction'i rollback
        // etmelidir (out.commitFailed ile bildirilir).
        if (!commit(affected[i], staged[i])) {
            out.commitFailed = true;
            out.error = L"component recompute uygulama hatasi (id=" +
                        std::to_wstring(static_cast<std::uint64_t>(affected[i])) + L")";
            return out;
        }
        out.committed.push_back(affected[i]);
    }
    return out;
}

} // namespace mm
