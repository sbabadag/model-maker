#pragma once

// MM-8b/3 — OTOMATIK SEMATIK (ortografik izdusum, GERCEK mm, tek olcek).
//
// Komponent diyalogundaki 2B sematik artik elle (rectP/circleP sabit
// koordinat) CIZILMEZ. GenerationPlan + MemberGeometry'den, secilen gorunum
// yonunde ortografik izdusumle OTOMATIK uretilir: levhalar gri dolgulu
// cokgen, uyeler profil kesitinden (h/b/tw/tf) kurulan kisa prizma, bulonlar
// gercek capta (govde d / delik d0 / bas-somun s / pul D), kesimler uye
// ucuna yansitilir. TUM tipler (yerlesik 4 tip + paketler) ayni yoldan gider.
//
// BULON GORUNUS KONVANSIYONU: bulonlar 2B izdusumde KENDI EKSENLERINE DIK
// kesitten (bas/delik/pul daire olarak) cizilir — mevcut elle sematik de
// boyleydi. Boylece "bas genisligi / levha kalinligi" gibi oranlar fiziksel
// dogru okunur. Levhalar ve uyeler GERCEK ortografik izdusum alir.
//
// Qt/OCCT'siz, windows.h'siz — Linux'ta ctest ile dogrulanir. Ayni girdi ->
// ayni cizim (Linux == mingw): saf double matematik, platform bagimliligi yok.

#include "model_maker/component_engine.hpp"
#include "model_maker/geometry.hpp"

#include <string>
#include <vector>

namespace mm {

enum class SchematicViewDir { Front, Side, Top };

enum class SchematicPrimKind { Polygon, Polyline, Circle, Annulus, Line };

// Tek cizim ilkeli (GERCEK mm koordinatlarinda; x saga, y yukari).
struct SchematicPrim {
    SchematicPrimKind kind{SchematicPrimKind::Line};
    std::vector<Vec2> pts;       // Polygon (kapali, saat yonu) / Polyline
    double cx{0.0}, cy{0.0};     // Circle / Annulus merkezi
    double r{0.0};               // Circle yaricapi / Annulus dis yaricap
    double rInner{0.0};          // Annulus ic yaricap (0 = dolu daire)
    std::string role;            // semantik etiket: plateKey | "column" | "beam" | "bolt"
    std::string detail;          // "bolt" icin: head | nut | shank | hole | washer
    bool dashed{false};
    bool filled{false};
    bool isPlate{false};         // levha -> renderer gri (kPlateGreyRgb) cizer
};

struct SchematicDrawing {
    std::vector<SchematicPrim> prims;
    double minX{0.0}, minY{0.0}, maxX{0.0}, maxY{0.0};
    bool ok{false};
    std::wstring error;
};

// Ortografik sematik uretir. `view` = izdusum yonu (Front = iki uyeye dik
// elevasyon — varsayilan; Side = kiris ekseni boyunca; Top = ana uye ekseni
// boyunca). Deterministik; plan bozuksa ok=false + Turkce neden.
SchematicDrawing buildComponentSchematic(const GenerationPlan& plan,
                                         const ComponentInput& input,
                                         SchematicViewDir view) noexcept;

}  // namespace mm
