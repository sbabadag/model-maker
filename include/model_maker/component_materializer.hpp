#pragma once
#ifdef MM_HAS_OCC
// COMPONENT MATERIALIZER (C2/C3) — ortak, tip-agnostik uretim katmani.
//
// ComponentBuilder/ComponentPlan'in urettigi roleKey'li child'lari (Plate,
// BoltGroup, Cut/Hole, Weld) gercek Document + OccSolidStore nesnelerine
// cevirir. Yeni bir component tipi (Fin Plate, Base Plate, ...) icin
// Document/Application icine AYRI hard-coded switch/case EKLEMEZ — bu katman
// ComponentPlan.children'i (roleKey -> kind -> params) iter eder ve ortak
// primitive ureticileri cagirir.
//
// IKI ASAMALI (atomic): prepare() geometriyi uretir ve DOGRULAR (Document
// MUTASYONU YOK), commit() tek undo kaydi altinda uygular. Prepare basarisizsa
// commit CAGIRILMAZ (kismi component uretimi yoktur).
//
// WeldSpec icin GERCEK weld geometri motoru HENUZ YOKTUR — sessizce "basari"
// URETILMEZ; prepare() acik Unsupported hatasi dondurur (C2 kurali).
//
// Bu baslik OCCT bilir (TopoDS_Shape); yalnizca MM_HAS_OCC derleme birimleri
// include eder. windows.h yok; Linux'ta gercek OCCT ile derlenir/test edilir.

#include "model_maker/component_framework.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"

#include <TopoDS_Shape.hxx>

#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

// Ortak materializer. Document + OccSolidStore'a baglidir; component tipine OZEL
// kod YOKTUR (yalnizca ComponentPlan.children'daki kind'lere dispatch).
class ComponentMaterializer {
public:
    explicit ComponentMaterializer(Document& doc);

    // --- PREPARE -------------------------------------------------------------
    // Her child icin gercek OCCT geometrisini uretir ve dogrular. Document
    // DEGISMEZ. Basarisizlikta ok=false + error (kismi uretim yok).
    struct PreparedChild {
        ComponentChildRecord record;   // roleKey + kind + uretim parametreleri
        TopoDS_Shape solid;            // Plate: levha kati; BoltGroup: montaj kati
        BoltGroup boltGroup;           // BoltGroup: parcalari cozumlenmis
        CutModifier cut;               // Cut/Hole: hedef cozumlenmis modifier
        bool weldUnsupported{false};   // Weld: motor yok (acik Unsupported)
    };
    struct Prepared {
        bool ok{false};
        std::vector<PreparedChild> children;  // topolojik (bagimlilik) sirada
        std::wstring error;
    };

    // plan: roleKey koruyan uretim plani. mainId/secondaryIds: kullanici parcalari
    // (SAHIPLENMEZ, yalniz referanslanir). Weld varsa ok=false + Unsupported.
    Prepared prepare(const ComponentPlan& plan, EntityId mainId,
                     const std::vector<EntityId>& secondaryIds) const;

    // --- COMMIT --------------------------------------------------------------
    // prepare ciktisini TEK undo kaydi altinda Document'a uygular. Basarisizlikta
    // rollbackOpenRecord() ile kismi degisiklik GERI ALINIR. record: component
    // kaydi (typeId + params + main/secondary); children tablosu + owned id'ler
    // burada doldurulur. Donus: roleKey -> EntityId eslemesi + component id.
    struct CommitResult {
        bool ok{false};
        std::unordered_map<std::string, EntityId> roleToEntity;
        EntityId componentId{kInvalidEntityId};
        std::vector<EntityId> ownedPlateIds;
        std::vector<EntityId> ownedBoltGroupIds;
        std::wstring error;
    };
    CommitResult commit(const Prepared& prep, ComponentInstanceRecord record);

    // --- Yardimci ------------------------------------------------------------
    // PlateSpec -> dikdortgen prizma (box) solid. Gecersiz boyut/taban -> null.
    static TopoDS_Shape buildPlateSolid(const PlateSpec& p);

    // Bir hedefin ETKIN kesme modifier'larini taban B-Rep'e uygular (kesme
    // zinciri) ve sonucu YENI surum olarak yazar. Application::recomputeCutTarget
    // ile ayni cekirdek; GUI/profil katalogu BAGIMLILIGI YOKTUR (yalniz Document
    // + OccSolidStore). Basarisizlikta belge DEGISMEZ.
    void applyCutsFor(EntityId target);

private:
    Document& doc_;
    OccSolidStore* store_;

    // Topolojik sira (Kahn, lexicographic tie-break) — bagimliliklar once.
    static std::vector<std::size_t> orderChildren(const std::vector<PreparedChild>& children,
                                                  std::wstring& error);
};

}  // namespace mm
#endif  // MM_HAS_OCC
