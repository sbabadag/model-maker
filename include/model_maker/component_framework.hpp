#pragma once

// UNIVERSAL COMPONENT FRAMEWORK (overnight 2026-10-19)
//
// Tekla tarzi yeni bir baglanti (End Plate, Fin Plate, Base Plate, Gusset,
// Splice, ...) gelistiren programcinin ORTAK CAD mekanizmalarini (plate solid,
// BoltGroup, hole/fitting modifier, weld, EntityId, ownership, undo, MMW8,
// recompute) YENIDEN YAZMADAN yalnizca (1) parametrelerini, (2) main/secondary
// input rollerini, (3) kullanacagi ortak objeleri (roleKey'li), (4) objeler
// arasi parametrik iliskileri ve (5) tipe ozel geometri kurallarini
// tanimlamasini saglar.
//
// BU DOSYA SAF VERI MODELIDIR (OCCT'siz, windows.h'siz). Linux'ta derlenir ve
// birim testleriyle dogrulanir. Gercek OCCT materialization ayri katmandadir
// (ComponentMaterializer arayuzu — Application/occ_geometry tarafinda).
//
// SOURCE OF TRUTH (mevcut sistemler — yeniden yazilmaz):
//   - component_engine.hpp  : typed param semasi + ComponentRegistry + GenerationPlan
//   - component_recompute.hpp: ComponentDependencyGraph + RecomputePlanner + atomicRecompute
//   - constraint_system.hpp : rank-tabanli pozisyon kisit cozucu (HandleKey)
//   - magnetic_frame.hpp    : ComponentLocalFrame + LocalPlacement
//   - geometry_reference_resolver.hpp : GeometryReference (EntityId + semantic role)
//   - component_engine.hpp  : ComponentInstanceRecord (persistent kayit)
//
// BU KATMANIN SAGLADIGI YENI PARCALAR:
//   1) ComponentObjectSpec  : roleKey -> tipli ortak nesne (Plate/BoltGroup/Cut/
//                             Weld/Construction). roleKey KARARLI string'dir;
//                             materialization'da roleKey->EntityId eslemesi
//                             recompute ve MMW8 sonrasi stabil kalir.
//   2) ComponentRelationshipGraph : geometric / dependency / ownership iliskilerini
//                             AYRI tutar; cycle / unresolved / conflict / ambiguous
//                             acik status uretir.
//   3) ComponentBuilder     : fluent, tip guvenli API. Yeni component tanimini
//                             object + relationship + roleKey olarak kurar.
//   4) ComponentMaterializer (arayuz): GenerationPlan + ComponentObjectSpec ->
//                             Document commit (OCCT tarafinda implemente edilir).
//
// PIPELINE (her component tipi):
//   Definition -> Resolve Inputs -> Validate Params -> Resolve Refs ->
//   Solve Constraints -> Build GenerationPlan -> Validate Geometry ->
//   Materialize OCCT -> Atomic Document Commit -> Recompute/Undo/Redo/MMW8

#include "model_maker/bolt.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/constraint_system.hpp"
#include "model_maker/cut_modifier.hpp"
#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mm {

// --- COMPONENT OBJECT SPEC (roleKey -> tipli ortak nesne) --------------------

enum class ComponentObjectKind {
    Plate,
    BoltGroup,
    Cut,
    Weld,
    ConstructionPoint,
    ConstructionLine,
    ConstructionPlane,
};

// Insaat yardimci nesneleri (plan asamasinda; EntityId materialization'da).
struct ConstructionPointSpec {
    Vec3 position{};
};
struct ConstructionLineSpec {
    Vec3 origin{};
    Vec3 dir{1.0, 0.0, 0.0};
};
struct ConstructionPlaneSpec {
    Vec3 origin{};
    Vec3 normal{0.0, 0.0, 1.0};
};

// Bir component'in SAHIP OLACAGI veya referanslayacagi ortak nesne. roleKey
// KARARLI kimliktir ("main_plate", "beam_bolts", "top_stiffener", ...).
// Ayni turden birden fazla nesne olabilir (her biri farkli roleKey).
struct ComponentObjectSpec {
    std::string roleKey;
    ComponentObjectKind kind{ComponentObjectKind::Plate};

    std::optional<PlateSpec> plate;
    std::optional<BoltGroup> boltGroup;
    std::optional<CutModifier> cut;
    std::optional<WeldSpec> weld;
    std::optional<ConstructionPointSpec> point;
    std::optional<ConstructionLineSpec> line;
    std::optional<ConstructionPlaneSpec> plane;
};

// roleKey'in gecerli/tekil oldugunu dogrular (bos degil, sadece [A-Za-z0-9_]).
bool isValidRoleKey(const std::string& roleKey) noexcept;

// --- RELATIONSHIP GRAPH ------------------------------------------------------

// Iliski turleri uc AYRI aileye bolunur:
//   * Geometric  : Coincident, Offset, Parallel, Perpendicular, FaceContact,
//                  Align, FollowFrame, SizeFromMember, SizeFromParameter
//   * Dependency : AttachedTo, DependsOn
//   * Ownership  : OwnedBy, Cuts, Welds
// Grafik bu aileleri ayri tutar; cycle yalnizca DEPENDENCY (AttachedTo/DependsOn)
// ve OWNERSHIP yonlu kenarlarinda kontrol edilir (geometric iliskiler ciftsizdir).
enum class RelationshipKind {
    Coincident,
    Offset,
    Parallel,
    Perpendicular,
    FaceContact,
    Align,
    FollowFrame,
    SizeFromMember,
    SizeFromParameter,
    AttachedTo,
    Cuts,
    Welds,
    OwnedBy,
    DependsOn,
};

// Bir iliskinin cozum/kontrol durumu.
enum class RelationshipStatus {
    Ok,
    Unresolved,   // referans (roleKey) bulunamadi
    Conflict,     // tutarsiz iliski (orn. hem Parallel hem Perpendicular)
    Ambiguous,    // ayni amac icin birden fazla gecerli secim
    Cyclic,       // dependency/ownership dongusu
};

bool isGeometricRelationship(RelationshipKind k) noexcept;
bool isDependencyRelationship(RelationshipKind k) noexcept;
bool isOwnershipRelationship(RelationshipKind k) noexcept;

// Bir iliski kenari. from/to = roleKey (kullanici parcalari icin ozel rezerve
// roleKey'ler: "main", "secondary_0", "secondary_1", ... kullanilir).
struct Relationship {
    std::string from;
    std::string to;
    RelationshipKind kind{RelationshipKind::Coincident};
    Vec3 offset{};       // Align/Offset icin yonlu ofset
    double distance{0.0}; // Offset icin skaler mesafe
    bool enabled{true};
};

// Iliski grafigi: geometric + dependency + ownership ayri. validate() su
// durumlari uretir:
//   - Unresolved: bir iliskinin from/to roleKey'i nesne listesinde (veya rezerve
//     main/secondary roleKey'lerinde) yok.
//   - Conflict: ayni ikili uzerinde celiskili geometric iliskiler.
//   - Cyclic: AttachedTo/DependsOn/OwnedBy yonlu kenarlarinda dongu.
class ComponentRelationshipGraph {
public:
    void add(const Relationship& r);
    const std::vector<Relationship>& relationships() const noexcept { return relationships_; }

    struct Validation {
        bool ok{false};
        RelationshipStatus status{RelationshipStatus::Ok};
        std::vector<std::string> problems;  // aciklama + ilgili roleKey'ler
    };

    // objects: component'in tanimladigi nesneler (roleKey kaynaklari). Rezerve
    // "main"/"secondary_N" roleKey'leri ayrica gecerli sayilir.
    Validation validate(const std::vector<ComponentObjectSpec>& objects) const;

    // Grafik uzerinde DFS ile yonlu dongu tespiti (dependency + ownership).
    // Dongudeki roleKey'leri dondurur (bos = dongu yok).
    std::vector<std::string> findCycles() const;

    std::size_t count() const noexcept { return relationships_.size(); }

private:
    std::vector<Relationship> relationships_;
};

// --- COMPONENT BUILDER (fluent, tip guvenli) ---------------------------------

// Yeni bir component tanimini object + relationship + roleKey olarak kurar.
// Ornek (kavramsal; gercek C++ API saglam ve tip guvenli):
//
//   ComponentBuilder b;
//   b.plate("fin_plate", plateSpec)
//    .boltGroup("beam_bolts", boltGroup)
//    .cut("beam_fit", fitting)
//    .weld("fin_weld", weldSpec)
//    .align("fin_plate", "secondary_0")
//    .faceContact("fin_plate", "main")
//    .sizeFromMember("fin_plate", "secondary_0")
//    .attach("beam_bolts", "fin_plate")
//    .cuts("beam_fit", "secondary_0")
//    .ownedBy("fin_plate", "main");
//   auto result = b.build();
//
// build() dogrular: roleKey tekilligi + iliski gecerliligi + cycle. Basarisizsa
// ok=false ve error dolu; materializer'a GECILMEZ (kismi uretim yok).
class ComponentBuilder {
public:
    ComponentBuilder() = default;

    // --- Ortak nesneler ---
    ComponentBuilder& plate(const std::string& roleKey, const PlateSpec& s);
    ComponentBuilder& boltGroup(const std::string& roleKey, const BoltGroup& s);
    ComponentBuilder& cut(const std::string& roleKey, const CutModifier& s);
    ComponentBuilder& weld(const std::string& roleKey, const WeldSpec& s);
    ComponentBuilder& point(const std::string& roleKey, const Vec3& pos);
    ComponentBuilder& line(const std::string& roleKey, const Vec3& origin, const Vec3& dir);
    ComponentBuilder& plane(const std::string& roleKey, const Vec3& origin, const Vec3& normal);

    // --- Iliskiler ---
    ComponentBuilder& coincident(const std::string& a, const std::string& b);
    ComponentBuilder& offset(const std::string& a, const std::string& b, double distance);
    ComponentBuilder& parallel(const std::string& a, const std::string& b);
    ComponentBuilder& perpendicular(const std::string& a, const std::string& b);
    ComponentBuilder& faceContact(const std::string& a, const std::string& b);
    ComponentBuilder& align(const std::string& a, const std::string& b, const Vec3& offset = {});
    ComponentBuilder& followFrame(const std::string& a, const std::string& b);
    ComponentBuilder& sizeFromMember(const std::string& obj, const std::string& member);
    ComponentBuilder& sizeFromParameter(const std::string& obj, const std::string& param);
    ComponentBuilder& attachedTo(const std::string& a, const std::string& b);
    ComponentBuilder& cuts(const std::string& cutter, const std::string& target);
    ComponentBuilder& welds(const std::string& weld, const std::string& partA, const std::string& partB);
    ComponentBuilder& ownedBy(const std::string& child, const std::string& owner);
    ComponentBuilder& dependsOn(const std::string& a, const std::string& b);

    // Ham iliski ekle (yardimcilar disinda ozel iliski icin).
    ComponentBuilder& relationship(const Relationship& r);

    struct Result {
        bool ok{false};
        std::vector<ComponentObjectSpec> objects;
        ComponentRelationshipGraph relationships;
        std::wstring error;
    };

    Result build() const;

private:
    std::vector<ComponentObjectSpec> objects_;
    ComponentRelationshipGraph graph_;
    std::wstring error_;  // addObject hatasi (tekrar/gecersiz roleKey) -> build() ok=false
};

// --- MATERIALIZER ARAYUZU (OCCT katmani implemente eder) ---------------------

// GenerationPlan + ComponentObjectSpec'leri gercek Document/OCCT nesnelerine
// cevirir. Bu arayuzun implementasyonu OCCT'ye (TopoDS_Shape) ve Document'a
// baglidir; BU BASLIK OCCT BILMEZ (arayuz turu olarak opaque void* kullanilir).
//
// Materializer sözlesmesi:
//   - roleKey -> EntityId eslemesi bu katmanda kurulur ve STABIL kalir
//     (recompute + MMW8 sonrasi ayni roleKey ayni EntityId'yi yeniden uretir).
//   - Kullanici parcalari (main/secondary) SAHIPLENMEZ, yalniz referanslanir.
//   - Basarisiz materialization kismi belge degisikligi YAPMAZ (atomic).
class ComponentMaterializer {
public:
    virtual ~ComponentMaterializer() = default;

    struct MaterializeResult {
        bool ok{false};
        std::unordered_map<std::string, EntityId> roleToEntity;
        std::wstring error;
    };

    // objects: roleKey'li ortak nesneler. mainId/secondaryIds: kullanici parcalari.
    // Girdi -> cikti EntityId eslemesi (stable roleKey). Basarisizsa ok=false.
    virtual MaterializeResult materialize(const std::vector<ComponentObjectSpec>& objects,
                                          EntityId mainId,
                                          const std::vector<EntityId>& secondaryIds) = 0;
};

} // namespace mm
