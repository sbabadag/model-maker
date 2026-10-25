#pragma once

// GEOMETRY REFERENCE RESOLVER (Phase R2d-1) — Tekla tarzi component referans
// cozumleyici ARAYUZU. Portable (OCCT'siz); gercek B-Rep face cozumu ayri bir
// adapter (occ_contact_resolver) uzerinden gelir ve bu katmana SIZMAZ.
//
// Bir component alt nesnesi (levha, bulon grubu, delik, weld, construction
// object) bir GEOMETRI REFERANSINA baglanir. Referans kaliciligi EntityId +
// role + faceRole + lokal cerceve + secici uzerinden; COZULEN duzlem yalnizca
// runtime cache'tir (resolved/plane) — transient TopoDS_Face kimligine veya
// B-Rep face index'ine kalici baglanti KURULMAZ.
//
// Referans turleri:
//   - Main/Secondary semantik yuzu (SemanticFaceRole)
//   - MemberStart / MemberEnd
//   - Construction Plane (adlandirilmis insaat duzlemi)
//   - Component Plane (baska component'in sahip oldugu duzlem)
//   - Gercek B-Rep planar face (OCCT adapter tarafindan cozulur)
//   - MemberLocalFrame (cercevenin kendisi)
//
// Cozulemeyen referans icin acik UNRESOLVED donulur; sessizce yanlis duzlem
// secilmez. Basarisiz cozum mevcut saglam geometriyi DEGISTIRMEZ (cagiran
// sorumlulugu — bu katman yalnizca cozum sonucunu raporlar).

#include "model_maker/contact_resolver.hpp"
#include "model_maker/reference_geometry.hpp"

#include <map>
#include <optional>
#include <string>

namespace mm {

// Referans turu: bir component alt nesnesi NEREYE baglanir.
enum class GeometryReferenceKind {
    None,
    SemanticFace,       // uyenin semantik yuzu (faceRole gerekli)
    MemberFrame,        // uyenin lokal cercevesi (origin + u/v/w)
    MemberStart,        // uye baslangici (normal -w)
    MemberEnd,          // uye sonu (normal +w)
    ConstructionPlane,  // adlandirilmis insaat duzlemi
    ComponentPlane,     // baska component'in sahip oldugu duzlem
};

// KALICI geometri referansi. Kalicilik kaynagi EntityId + role + faceRole +
// lokal cerceve + secici; cozulen duzlem RUNTIME cache'tir.
struct GeometryReference {
    GeometryReferenceKind kind{GeometryReferenceKind::None};
    EntityId source{kInvalidEntityId};          // kaynak uye / duzlem varligi
    ComponentInputRole role{ComponentInputRole::None};
    SemanticFaceRole faceRole{SemanticFaceRole::None};
    std::optional<MemberLocalFrame> localFrame;  // opsiyonel cerceve override
    std::string selector;                        // opsiyonel geometrik secici
    // Construction/Component duzlemi icin acik duzlem (varlik lookup bu dilimde
    // yok; duzlem saglanmazsa UNRESOLVED donulur — fail-closed).
    std::optional<ReferencePlane> explicitPlane;
};

// Cozum ciktisi.
struct ResolvedGeometry {
    bool resolved{false};
    ResolutionStatus status{ResolutionStatus::Unresolved};
    ReferencePlane plane{};          // SemanticFace / Start / End / duzlem icin
    MemberLocalFrame frame{};        // MemberFrame icin (ve plane ile birlikte)
};

// Cozumleyici arayuzu (portable). OCCT'den bagimsizdir; gercek B-Rep yuzu ayri
// bir adapter cozer ve ayni arayuzun SemanticFace kismini oradan besler.
class GeometryReferenceResolver {
public:
    virtual ~GeometryReferenceResolver() = default;
    virtual ResolvedGeometry resolve(const GeometryReference& ref) const = 0;
};

// Lokal kesit geometrisi tabanli TEMEL cozumleyici (OCCT'siz). SemanticFace'i
// resolveSemanticFacePlane ile, MemberFrame'i cercevenin kendisiyle,
// MemberStart/End'i ucu duzlemleriyle, Construction/Component duzlemini
// explicitPlane ile cozer. Uye kayitlari (cerceve + boyut + boy) cozumleme
// sirasinda source EntityId ile aranir.
class BasicGeometryReferenceResolver final : public GeometryReferenceResolver {
public:
    struct MemberRecord {
        MemberLocalFrame frame{};
        SectionDims dims{};
        double length{0.0};
    };
    using MemberMap = std::map<EntityId, MemberRecord>;

    explicit BasicGeometryReferenceResolver(MemberMap members)
        : members_(std::move(members)) {}

    ResolvedGeometry resolve(const GeometryReference& ref) const override;

private:
    MemberMap members_;
};

} // namespace mm
