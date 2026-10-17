#pragma once

// REFERENCE GEOMETRY (Phase R1) — Tekla tarzi Custom Component referans
// duzlemi altyapisi.
//
// Amac: bir component nesnesini (levha, bulon grubu, delik) bir UYENIN GERCEK
// yuzeyine/sinir duzlemine baglamak. Tekla'nin "Boundary plane" kavraminin
// karsiligi: profil kesitini saran sinir kutusunun (bbox) yuzleri. Bu ilk
// dilimde yalnizca bbox sinir duzlemi cozulur; gercek B-Rep face cozumu
// (Outline plane) sonraki dilimde eklenir.
//
// Referanslar EntityId + semantik tur uzerinden COZULUR (transient TopoDS_Face
// kimligine veya face index'ine kalici baglanti kurulmaz). Cozulemeyen referans
// icin acik hata/fallback donulur — sessizce yanlis duzlem secilmez.
//
// windows.h yok; Linux'ta derlenir/test edilir.

#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"

namespace mm {

// Bir referans duzlemi: origin (duzlem uzerinde bir nokta) + birim normal.
// normal, referans alinan yuzeyden DISARI bakar.
struct ReferencePlane {
    Vec3 origin{};
    Vec3 normal{0.0, 0.0, 1.0};
};

// Uyenin LOKAL koordinat cercevesi. Dondurulmus kolonlar icin world-axis
// bbox'a bagli KALINMAZ; yuzler bu cerceveden + kesit katalogundan turetilir.
// (Slice 2b'de kesit katalogu + uyenin gercek donus matrisiyle doldurulacak;
// eksenler ortonormal olmali.)
struct MemberLocalFrame {
    Vec3 origin{};            // uye baslangic noktasi (member start)
    Vec3 u{1.0, 0.0, 0.0};    // kesit GUCLU eksen (I-kesitte flans genislik yonu)
    Vec3 v{0.0, 1.0, 0.0};    // kesit ZAYIF eksen (I-kesitte govde derinlik yonu)
    Vec3 w{0.0, 0.0, 1.0};    // boy eksen (member axis)
};

// Component'in uyeleri hangi rolle bagladigi (Tekla main/secondary).
enum class ComponentInputRole {
    None,
    Main,       // ana parca (End Plate'te kolon)
    Secondary,  // ikincil parca (End Plate'te kiris)
};

// Uye uzerindeki SEMANTIK yuz rolleri. Flans DIS yuzu, flans KENARI ve govde
// yuzu BIRBIRINDEN FARKLIDIR (geometrik referans DUZLEMI ile sinirli B-Rep
// temas yuzu de farklidir). I-kesit ofsetleri:
//   FlangeOuter +/- : ±h/2 boyunca v (ust/alt flans dis yuzu)
//   Web        +/- : ±tw/2 boyunca u (govde dis yuzu)
//   FlangeEdge +/- : ±b/2 boyunca u (flans ucu)
//   MemberStart/End: normal ∓/+w (uye uclari)
enum class SemanticFaceRole {
    None,
    FlangeOuterPositive,  // +h/2 boyunca v
    FlangeOuterNegative,  // -h/2 boyunca v
    WebPositive,          // +tw/2 boyunca u
    WebNegative,          // -tw/2 boyunca u
    FlangeEdgePositive,   // +b/2 boyunca u
    FlangeEdgeNegative,   // -b/2 boyunca u
    MemberStart,          // uye baslangici, normal -w
    MemberEnd,            // uye sonu, normal +w
};

// KALICI yuz referansi. Cozum ADAPTER tarafindan yapilir (OCCT'den bagimsiz):
// source + role + faceRole + opsiyonel lokal cerceve/secici → ReferencePlane.
// Cozulemezse resolved=false kalir (cozucu UNRESOLVED raporlar); SESSIZCE
// yanlis duzlem secilmez. Kalici referans transient TopoDS_Face kimligine veya
// B-Rep face index'ine BAGLANMAZ.
struct FaceReference {
    EntityId source{kInvalidEntityId};  // kaynak uye (0 = yok/gecersiz)
    ComponentInputRole role{ComponentInputRole::None};
    SemanticFaceRole faceRole{SemanticFaceRole::None};
    std::optional<MemberLocalFrame> localFrame;  // opsiyonel lokal cerceve
    std::string selector;  // opsiyonel geometrik secici (Slice 2b'de somutlasir)
    // Adapter cozum ciktisi:
    bool resolved{false};
    ReferencePlane plane{};
};

// bbox sinir duzlemini cozer: outwardDir yonune BAKAN yuzu (dis normali
// outwardDir ile EN PARALEL olan yuz) dondurur. `bmin`/`bmax` bbox kose noktalari.
// Yuzun normali outwardDir yonune bakar; origin, yuzun merkez noktasidir.
ReferencePlane resolveBoundaryPlaneFacing(const Vec3& bmin, const Vec3& bmax,
                                          const Vec3& outwardDir) noexcept;

} // namespace mm
