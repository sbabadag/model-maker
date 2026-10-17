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

#include "model_maker/geometry.hpp"

namespace mm {

// Bir referans duzlemi: origin (duzlem uzerinde bir nokta) + birim normal.
// normal, referans alinan yuzeyden DISARI bakar.
struct ReferencePlane {
    Vec3 origin{};
    Vec3 normal{0.0, 0.0, 1.0};
};

// Desteklenen referans kaynaklari (semantik tur). Ilk dilimde yalnizca
// MainPartBoundary kullanilir; digerleri altyapiyi test etmek icin ayristirilir.
enum class PlaneReferenceKind {
    MainPartBoundary,      // main parcanin sinir kutusu (bbox) yuzu
    SecondaryPartBoundary, // secondary parcanin bbox yuzu
    MainPartCenter,        // main parcanin merkez duzlemi
    SecondaryPartCenter,   // secondary parcanin merkez duzlemi
};

// bbox sinir duzlemini cozer: outwardDir yonune BAKAN yuzu (dis normali
// outwardDir ile EN PARALEL olan yuz) dondurur. `bmin`/`bmax` bbox kose noktalari.
// Yuzun normali outwardDir yonune bakar; origin, yuzun merkez noktasidir.
ReferencePlane resolveBoundaryPlaneFacing(const Vec3& bmin, const Vec3& bmax,
                                          const Vec3& outwardDir) noexcept;

} // namespace mm
