#pragma once

// CONTACT RESOLVER (Phase R2b) — I-kesit uyesinin GERCEK temas yuzunu ve temas
// noktasini cozer. End Plate/B-Rep temas hesabini Base Plate, Fin Plate, Gusset
// ve Splice'te yeniden kullanilabilir kilar.
//
// Ayrimlar (ONEMLI):
//   - Geometrik referans DUZLEMI (sonsuz) ile gercek SINIRLI temas yuzu farklidir.
//   - Flans DIS yuzu / flans KENARI / govde yuzu ayri semantic rollerdir.
//   - Dondurulmus kolon icin world-axis bbox'a bagli KALINMAZ: yuzler uyenin
//     LOKAL cercevesinden (MemberLocalFrame) turetilir.
//
// COZUCU OCCT'DEN BAGIMSIZDIR: once lokal kesit geometrisinden semantik duzlem
// cozulur; gercek B-Rep yuz eslemesi ayri bir adapter uzerinden gelir (burada
// yalnizca lokal kesit geometrisi + sonlu yuz siniri dogrulamasi).

#include "model_maker/reference_geometry.hpp"

namespace mm {

// I-kesit boyutlari (mm). h = kesit yuksekligi, b = flans genisligi,
// tw = govde kalinligi, tf = flans kalinligi.
struct SectionDims {
    double h{0.0};
    double b{0.0};
    double tw{0.0};
    double tf{0.0};
};

// ProfileSectionFrame (width/height/axis) -> MemberLocalFrame.
// u = kesit genislik ekseni, v = kesit yukseklik ekseni, w = boy eksen.
MemberLocalFrame makeMemberLocalFrame(const ProfileSectionFrame& section,
                                      const Vec3& origin) noexcept;

// Uye cercevesinin ortonormal ve SAG ELLI (u x v == w) oldugunu dogrular.
bool isOrthonormalRightHanded(const MemberLocalFrame& frame, double tol = 1e-9) noexcept;

// Semantic bir yuzu sonsuz referans duzlemine cozer (origin + birim normal).
// `length`: uyenin boyu (MemberEnd icin origin + length*w). Gecersiz rol veya
// dejenere cerceve icin nullopt.
std::optional<ReferencePlane> resolveSemanticFacePlane(const MemberLocalFrame& frame,
                                                       const SectionDims& dims,
                                                       double length,
                                                       SemanticFaceRole role) noexcept;

// Temas cozum durumu.
enum class ResolutionStatus {
    Resolved,       // temas yuzu + temas noktasi belirlendi
    NoContact,      // eksen hicbir aday yuzu sinirlar icinde kesmiyor
    ParallelAxis,   // eksen aday duzlemlere paralel
    OutsideFace,    // kesim noktasi sonlu yuzun disinda
    AmbiguousFace,  // belirsiz yuz secimi
    Unresolved,     // referans/geometri eksik veya dejenere
};

// Temas cozumunun ciktisi.
struct ContactPlacement {
    ResolutionStatus status{ResolutionStatus::Unresolved};
    ReferencePlane referencePlane{};  // secilen sonsuz temas duzlemi
    Vec3 contactPoint{};              // kiris ekseni ile temas duzleminin KESIMI
    Vec3 outwardNormal{0, 0, 1};      // main'den secondary'ye dogru dis normal
    MemberLocalFrame localFrame{};    // main'in lokal cercevesi (aynen)
    EntityId sourceEntityId{kInvalidEntityId};
    SemanticFaceRole semanticFaceRole{SemanticFaceRole::None};
};

// MAIN uyenin (kolon) temas yuzunu ve temas noktasini cozer.
//
// `mainFrame`: kolonun lokal cercevesi. `mainDims`: kesit boyutlari.
// `mainLength`: kolon boyu. `beamAxisOrigin`: kiris baglanti ekseninde bir nokta
// (or. kirisin kolona yakin ucu). `beamAxisDir`: kiris eksen yonu (birim), KOLONA
// DOGRU (yaklasma yonu). `preferredRole`: tercih edilen yuz ailesi; simetrik bir
// rol ise isaret yaklasma yonune gore secilir.
//
// Temas noktasi, yuz MERKEZINDEN degil, kiris ekseninin secilen duzlemle
// KESIMINDEN hesaplanir — kirisin gercek baglanti kotu korunur. Paralel eksen,
// yuzey disi kesim ve belirsiz secim icin acik hata (ResolutionStatus) doner;
// gecersiz durumda component RASTGELE yerlesTIRILMEZ.
ContactPlacement resolveContactPlacement(const MemberLocalFrame& mainFrame,
                                         const SectionDims& mainDims,
                                         double mainLength,
                                         const Vec3& beamAxisOrigin,
                                         const Vec3& beamAxisDir,
                                         SemanticFaceRole preferredRole =
                                             SemanticFaceRole::FlangeOuterPositive) noexcept;

} // namespace mm
