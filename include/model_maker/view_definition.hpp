#pragma once

// IKI NOKTALI GORUNUS (Tekla "Create view by two points").
// P1 -> P2 dogrultusu ekranda SOLDAN SAGA, dunya Z'si YUKARI; bakis yonu
// XY duzlemine PARALEL (gorunus duzlemi XY'ye DIK). Z farki gorunusu
// egmez — yalniz P1->P2'nin yatay (XY) izdusumu kullanilir.
// Tasinabilir cekirdek (windows.h yok) — Linux testinde dogrulanir.

#include "model_maker/document.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/ucs.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace mm {

struct ViewDefinition {
    std::wstring name;
    Vec3 origin{};     // P1
    Vec3 right{1, 0, 0}; // ekran saga (yatay, birim)
    Vec3 up{0, 0, 1};    // ekran yukari (= dunya Z)
    Vec3 normal{0, -1, 0}; // izleyiciye dogru (right x up); bakis = -normal
    double length{};   // P1-P2 yatay mesafe
    // Derinlik dilimi (Tekla "view depth"): gorunus duzleminin onu/arkasi.
    double depthFront{1000.0}; // izleyici tarafi (+normal)
    double depthBack{1000.0};  // arka taraf (-normal)
};

inline constexpr double kDefaultViewDepth = 1000.0; // mm

inline std::optional<ViewDefinition> viewFromTwoPoints(const Vec3& p1, const Vec3& p2,
                                                       double depthFront = kDefaultViewDepth,
                                                       double depthBack = kDefaultViewDepth) {
    const double dx = p2.x - p1.x, dy = p2.y - p1.y;
    const double horizontal = std::sqrt(dx * dx + dy * dy);
    // Ayni nokta veya dusey cift (XY'de cakisan): yon tanimsiz.
    if (!(horizontal > 1e-6)) return std::nullopt;
    const auto sane = [](double d) {
        return std::isfinite(d) && d >= 0.0 ? d : kDefaultViewDepth;
    };
    ViewDefinition v;
    v.origin = p1;
    v.right = {dx / horizontal, dy / horizontal, 0.0};
    v.up = {0.0, 0.0, 1.0};
    // normal = right x up = (ry, -rx, 0): izleyici tarafi.
    v.normal = {v.right.y, -v.right.x, 0.0};
    v.length = horizontal;
    v.depthFront = sane(depthFront);
    v.depthBack = sane(depthBack);
    return v;
}

// PLAN (seviye) gorunusu: gorunus duzlemi XY'ye PARALEL, verilen Z kotunda.
// KULLANICI SARTI: "view eklemeye PLAN XY de belirtilen Z koordinatinda ekle".
// Izleyici YUKARIDAN bakar (bakis = -Z), ekran sag +X, ekran yukari +Y.
// Derinlik dilimi burada DUSEYdir:
//   depthFront = kotun USTU  (izleyici tarafi, +Z)
//   depthBack  = kotun ALTINDA (-Z)
// Yani Z kotu "ne gorunur"u belirler (plan kesiti), iki noktali gorunuste
// oldugu gibi ekran yonunu degil.
inline constexpr double kDefaultPlanDepthAbove = 3000.0; // mm, kotun ustu
inline constexpr double kDefaultPlanDepthBelow = 1000.0; // mm, kotun alti

inline std::optional<ViewDefinition> viewFromPlanLevel(
    double level, double depthAbove = kDefaultPlanDepthAbove,
    double depthBelow = kDefaultPlanDepthBelow) {
    if (!std::isfinite(level)) return std::nullopt;
    const auto sane = [](double d) {
        return std::isfinite(d) && d >= 0.0 ? d : kDefaultViewDepth;
    };
    ViewDefinition v;
    v.origin = {0.0, 0.0, level};
    v.right = {1.0, 0.0, 0.0};  // ekran saga = +X
    v.up = {0.0, 1.0, 0.0};     // ekran yukari = +Y (plan)
    v.normal = {0.0, 0.0, 1.0}; // right x up = izleyici tarafi (yukari)
    v.length = 0.0;             // plan gorunusunde P1-P2 hatti yok
    v.depthFront = sane(depthAbove);
    v.depthBack = sane(depthBelow);
    return v;
}

// Plan gorunus mu? (gorunus duzlemi XY'ye PARALEL -> normal ±Z.)
// Baslik/etiket metinlerini secmek icin kullanilir; kalici bir alan DEGIL.
inline bool isPlanView(const ViewDefinition& v) noexcept {
    return std::abs(v.normal.z) > 0.9;
}

// PARCA DUZLEMI GORUNUSU (Tekla "on part front/top/back/bottom plane" +
// "default views of part"): verilen parca cercevesi (Ucs::fromPartFrame
// ciktisi: xAxis=width, yAxis=height, zAxis=axis) uzerinde secilen duzlemin
// ViewDefinition'ini uretir. Origin = parca eksen orijini; sag-el ortonormal
// taban (normal = right x up) viewOnPartPlane ile kurulur.
inline ViewDefinition viewFromPartPlane(const Ucs& partFrame, PartPlane plane,
                                        const Vec3& origin) noexcept {
    const UcsView basis = viewOnPartPlane(partFrame, plane);
    ViewDefinition v;
    v.origin = origin;
    v.right = basis.right;
    v.up = basis.up;
    v.normal = basis.normal;
    v.length = 0.0; // parca gorunusunde P1-P2 hatti yok
    v.depthFront = kDefaultViewDepth;
    v.depthBack = kDefaultViewDepth;
    return v;
}

// GRID CIZGILERI BOYUNCA GORUNUSLER (Tekla "along grid lines" / axis views):
// her aks cizgisi icin o cizginin duzlemine DIK bir gorunus uretilir.
//   X cizgileri (Y'ye paralel dikey duzlemler) -> normal +X, bakis -X
//   Y cizgileri (X'e paralel dikey duzlemler) -> normal +Y, bakis -Y
//   Z katlari (XY'ye paralel yatay duzlemler)   -> normal +Z (plan)
// Origin her cizginin grid origin'ine gore konumudur. Etiket ad olarak
// yazilir (bos etiket -> "Aks N"). Sag-el: normal = right x up.
inline std::vector<ViewDefinition> viewsAlongGridLines(const GridDefinition& grid) {
    std::vector<ViewDefinition> out;
    const Vec3 o = grid.origin;
    const auto nameFor = [](const std::string& label, std::size_t index) {
        std::wstring w;
        if (!label.empty()) {
            for (const char c : label) w.push_back(static_cast<wchar_t>(c));
        } else {
            const std::string n = std::to_string(index + 1);
            for (const char c : n) w.push_back(static_cast<wchar_t>(c));
        }
        return w;
    };
    // X akslari: duzlem YZ'ye paralel, x = o.x + xCoord.
    for (std::size_t i = 0; i < grid.xCoords.size(); ++i) {
        ViewDefinition v;
        v.origin = {o.x + grid.xCoords[i], o.y, o.z};
        v.right = {0.0, 1.0, 0.0};
        v.up = {0.0, 0.0, 1.0};
        v.normal = {1.0, 0.0, 0.0}; // right x up = +X
        v.length = 0.0;
        v.name = L"Grid " + nameFor(i < grid.xLabels.size() ? grid.xLabels[i] : std::string{}, i);
        out.push_back(v);
    }
    // Y akslari: duzlem XZ'ye paralel, y = o.y + yCoord. Bakisin +Y
    // tarafindan gelmesi icin normal = +Y; sag-el tutarliligi (normal =
    // right x up) icin right = -X, up = +Z kullanilir.
    for (std::size_t i = 0; i < grid.yCoords.size(); ++i) {
        ViewDefinition v;
        v.origin = {o.x, o.y + grid.yCoords[i], o.z};
        v.right = {-1.0, 0.0, 0.0};
        v.up = {0.0, 0.0, 1.0};
        v.normal = {0.0, 1.0, 0.0};
        v.length = 0.0;
        v.name = L"Grid " + nameFor(i < grid.yLabels.size() ? grid.yLabels[i] : std::string{}, i);
        out.push_back(v);
    }
    // Z katlari: duzlem XY'ye paralel (plan), z = o.z + zCoord.
    for (std::size_t i = 0; i < grid.zCoords.size(); ++i) {
        ViewDefinition v;
        v.origin = {o.x, o.y, o.z + grid.zCoords[i]};
        v.right = {1.0, 0.0, 0.0};
        v.up = {0.0, 1.0, 0.0};
        v.normal = {0.0, 0.0, 1.0}; // right x up = +Z (yukaridan plan)
        v.length = 0.0;
        v.name = L"Kat " + nameFor(i < grid.zLabels.size() ? grid.zLabels[i] : std::string{}, i);
        out.push_back(v);
    }
    return out;
}

// Sinir kutusu derinlik diliminde mi? (kutunun normal yonundeki izdusum
// araligi [-back, +front] ile kesisiyorsa gorunur.)
inline bool boundsInViewSlab(const Bounds3& b, const ViewDefinition& v) noexcept {
    const Vec3 c{(b.minimum.x + b.maximum.x) * 0.5, (b.minimum.y + b.maximum.y) * 0.5,
                 (b.minimum.z + b.maximum.z) * 0.5};
    const Vec3 h{(b.maximum.x - b.minimum.x) * 0.5, (b.maximum.y - b.minimum.y) * 0.5,
                 (b.maximum.z - b.minimum.z) * 0.5};
    const double center = (c.x - v.origin.x) * v.normal.x + (c.y - v.origin.y) * v.normal.y +
                          (c.z - v.origin.z) * v.normal.z;
    const double radius = std::abs(h.x * v.normal.x) + std::abs(h.y * v.normal.y) +
                          std::abs(h.z * v.normal.z);
    return center + radius >= -v.depthBack && center - radius <= v.depthFront;
}

// Gorunusun ilk "sigdir" kutusu: dilimdeki modellerin sinirlari + P1/P2.
// Dilim bossa P1-P2 hattinin etrafinda makul bir kutu (bos ekran degil).
inline Bounds3 viewFitBounds(const std::vector<Bounds3>& modelBounds, const ViewDefinition& v) {
    const Vec3 p2{v.origin.x + v.right.x * v.length, v.origin.y + v.right.y * v.length, v.origin.z};
    Bounds3 box{{std::min(v.origin.x, p2.x), std::min(v.origin.y, p2.y), v.origin.z},
                {std::max(v.origin.x, p2.x), std::max(v.origin.y, p2.y), v.origin.z}};
    bool any = false;
    for (const auto& b : modelBounds) {
        if (!std::isfinite(b.minimum.x) || !std::isfinite(b.maximum.x) ||
            !std::isfinite(b.minimum.z) || !std::isfinite(b.maximum.z)) continue;
        if (!boundsInViewSlab(b, v)) continue;
        any = true;
        box.minimum = {std::min(box.minimum.x, b.minimum.x), std::min(box.minimum.y, b.minimum.y),
                       std::min(box.minimum.z, b.minimum.z)};
        box.maximum = {std::max(box.maximum.x, b.maximum.x), std::max(box.maximum.y, b.maximum.y),
                       std::max(box.maximum.z, b.maximum.z)};
    }
    if (!any) { // bos dilim: hattin dortte biri kadar dusey pay
        const double pad = std::max(v.length * 0.25, 500.0);
        // Plan gorunusunde P1-P2 hatti YOK (length == 0); o zaman gorunus
        // duzlemindeki iki yonde de pay ver, yoksa projeksiyon kutusu TEK
        // NOKTAYA coker ve fit3D anlamsiz bir olcek uretir. Elevasyonda
        // (length > 0) davranis DEGISMEZ: yalniz Z payi eklenirdi.
        const double padInPlane = v.length > 0.0 ? 0.0 : pad;
        box.minimum.x -= padInPlane;
        box.maximum.x += padInPlane;
        box.minimum.y -= padInPlane;
        box.maximum.y += padInPlane;
        box.minimum.z -= pad;
        box.maximum.z += pad;
    }
    return box;
}

// Gorunus duzlemi (ikincil viewport'un calisma duzlemi): cizim/snap bu
// dusey duzlemde yapilir (cephe cizimi).
inline WorkPlane viewWorkPlane(const ViewDefinition& v) noexcept {
    return WorkPlane{v.origin, v.right, v.up, v.normal};
}

// Profil kesiti REFERANS NORMALI: baslik plakalari bu duzleme PARALEL uretilir.
// KULLANICI SARTI: "profil cizilirken profil ust basligi workplane ile paralel
// olacak."
//  - Aktif IS DUZLEMININ normali kullanilir: kullanici egik bir UCS kurduysa
//    (or. egik cati duzlemi) kirislerin basliklari o duzleme oturur.
//  - ANCAK iki noktali gorunuste is duzlemi OTOMATIK olarak GORUNUS duzlemidir
//    (viewWorkPlane). Orada dunya Z kullanilir; aksi halde elevasyonda cizilen
//    her kirisin basligi BAKISA paralel olur ve kiris levha gibi "yatak"
//    gorunurdu. Kullanici gorunus icinde FARKLI bir UCS kurduysa (normali
//    gorunus duzleminden farkli) o kullanilir.
inline Vec3 profileSectionReferenceNormal(const WorkPlane& plane,
                                          const ViewDefinition* view) noexcept {
    const Vec3 worldUp{0.0, 0.0, 1.0};
    const auto unit = [](const Vec3& v) -> std::optional<Vec3> {
        const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (!(len > 1e-12)) return std::nullopt;
        return Vec3{v.x / len, v.y / len, v.z / len};
    };
    const auto planeNormal = unit(plane.normal);
    if (!planeNormal) return worldUp;
    if (view) {
        const auto viewNormal = unit(view->normal);
        if (viewNormal) {
            const double d = planeNormal->x * viewNormal->x + planeNormal->y * viewNormal->y +
                             planeNormal->z * viewNormal->z;
            if (d > 1.0 - 1e-9) return worldUp; // duzlem = otomatik gorunus duzlemi
        }
    }
    return *planeNormal;
}

} // namespace mm
