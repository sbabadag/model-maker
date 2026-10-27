#pragma once
// MODEL -> CIZIM BORU HATTI (P1): mark atama, 2B gorunus uretimi, A3 yerlesim, etiket/olcu.
//
//   markDocument()  : Document'taki tum parca/assembly'lere kullaniciya gorunen mark verir.
//   generateView()  : parca listesinden Front/Top/Side 2B gorunus (EntityId'ye BAGLI geometri).
//   placeViews()    : olcek secimi (standart olcekler) + A3 uzerinde yerlesim.
//   buildSheet()    : hepsini birlestirir -> DrawingSheet (model uzayindan ayri).
//
// SINIRLAR (bilincli; P1 kapsami):
//   * Girdi WireframeModel kenarlaridir. Gorunur cizgi = projeksiyon + yinelenenleri at.
//   * Hidden line (opsiyonel, varsayilan KAPALI): yalniz arka-yuz (back-face) testi; oklüzyon
//     YOK. Cukur/kavrik parcalarda (I profil) hatali olabilir -> deneysel.
//   * Projeksiyon MUHENDISLIK konvansiyonuyla burada tanimlidir (projectToView): Z yukari.
//     Camera::standardViewRotation / orthoProject (assembly.hpp) KASTEN kullanilmaz: Camera
//     ile olculdu (setView + project): Front'ta +Z ekranda ASAGI gidiyor, Right'ta +Z ekranda
//     YATAY (sola). Bu elevation cizimi icin ters/yatik olurdu. 3B goruntu kamerasina
//     DOKUNULMADI (kapsam disi); asil kamera davranisinin kasitli olup olmadigi ayrica sorulmali.
//   * Yalniz eksen-uyumlu uye boyu olculenir (properties'te eksen varsa ve gorunusle
//     ayni duzlemdeyse); aksi halde yalniz genel sinir olculeri.

#include "model_maker/document.hpp"
#include "model_maker/drawing.hpp"
#include "model_maker/marking.hpp"

#include <string>
#include <vector>

namespace mm::drawing {

struct DrawingOptions {
    SheetSize sheet{SheetSize::A3};
    ProjectionMethod projection{ProjectionMethod::ThirdAngle};
    std::vector<ViewKind> views{ViewKind::Front, ViewKind::Top, ViewKind::Side};
    bool hiddenLines{false};
    double viewGap{35.0};     // gorunusler arasi kagit mm (olcu/etiket payi)
    double padding{20.0};     // cizim alani ic payi (kagit mm)
    std::string project;
    std::string drawnBy;
    std::string date;
};

struct DocumentMarkSummary {
    std::size_t parts{0};
    std::size_t assemblies{0};
};

struct BuildResult {
    bool ok{false};
    std::string error;
    DrawingSheet sheet;
};

// 3B nokta -> gorunus-yerel ham 2B (olceksiz, ofsetsiz). Sag-el kurali, Z yukari:
//   Front : bakis +Y yonunde (goz -Y)  -> (x, z)
//   Top   : bakis -Z yonunde (goz +Z)  -> (x, y)
//   Side  : bakis -X yonunde (goz +X)  -> (y, z)   ("Right" gorunusu)
Vec2 projectToView(const Vec3& p, ViewKind kind) noexcept;

// Mark etiketinin KAGIT uzayindaki tahmini kutusu (GUI cizimi ile tutarli, hafif muhafazakar):
// yerlesim cakisma kontrolunde ve testlerde kullanilir.
Bounds2 markLabelBox(const MarkAnnotation& mark) noexcept;

// Cizime konu olabilecek parca mi? (nokta/konstruksiyon/kenarsiz modeller degil.)
bool isDrawablePart(const WireframeModel& model) noexcept;

// Tum parcalari (once) ve assembly'leri (sonra) isaretler. Idempotent.
DocumentMarkSummary markDocument(const Document& document, MarkRegistry& registry);

// subject: assembly ise uyeleri, degilse tek parca. Hata -> bos liste + error.
std::vector<EntityId> subjectParts(const Document& document, EntityId subject, DrawingKind& kind,
                                   std::string& error);

// Tek gorunus. Segmentler yerel koordinatta (min=(0,0)), olcek=1, origin=(0,0).
DrawingView generateView(const Document& document, const std::vector<EntityId>& parts,
                         EntityId subject, ViewKind kind, bool hiddenLines);

// En iyi standart olcegi secip gorunusleri sheet uzerine yerlestirir.
// Sigmazsa standart olmayan olcek (scaleIsStandard=false); alan sifir ise false.
bool placeViews(DrawingSheet& sheet, const DrawingOptions& options);

BuildResult buildSheet(const Document& document, MarkRegistry& registry, EntityId subject,
                       const DrawingOptions& options = {});

} // namespace mm::drawing
