#pragma once

// TEKLA-TARZI UCS + VIEW SISTEMI (P1).
//
// "Ucs" sag-el ortonormal bir cerceve (origin + X/Y/Z eksenleri) tanimlar;
// WorkPlane'in ustune kurulur ama eksen semantigi ACIK adlandirilmistir
// (X/Y/Z). View cikarimlari Camera::setViewBasis(right, up, center) ile
// dogrudan calisir (ekran saga = xAxis, ekran yukari = yAxis, izleyiciye
// dogru = zAxis). Bu baslik SADECE backend'dir (windows.h yok) — Linux'ta
// derlenir ve test edilir; UI baglantisi (Qt/Win32 komutlari) ayri katman.
//
// Kapsam:
//   - Global XY/XZ/YZ calisma duzlemi
//   - 1 / 2 / 3 noktadan UCS
//   - B-Rep yuzeyinden UCS (origin + normal)
//   - Parca koordinat sisteminden UCS (eksen + referans normali)
//   - Mevcut UCS uzerinde View
//   - 2 / 3 noktadan View
//   - Secilen yuzeye Dik View
//   - Named Views + View List
//   - Global UCS'ye donus (Ucs::world())

#include "model_maker/geometry.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mm {

struct Ucs {
    Vec3 origin{};
    Vec3 xAxis{1.0, 0.0, 0.0};
    Vec3 yAxis{0.0, 1.0, 0.0};
    Vec3 zAxis{0.0, 0.0, 1.0};

    // GLOBAL koordinat sistemi (identity). "Global UCS'ye donus" hedefi.
    static Ucs world() noexcept;

    // GLOBAL CALISMA DUZLEMLERI (Tekla work plane):
    //   planeXY: duzlem X-Y, normal +Z  (= world)
    //   planeXZ: duzlem X-Z, normal -Y  (cepheden bakis; Z ekran yukari)
    //   planeYZ: duzlem Y-Z, normal +X  (sagdan bakis; Z ekran yukari)
    static Ucs planeXY(Vec3 origin = {}) noexcept;
    static Ucs planeXZ(Vec3 origin = {}) noexcept;
    static Ucs planeYZ(Vec3 origin = {}) noexcept;

    // 1 NOKTADAN UCS: origin; eksenler dunya kalir (referans yonu yoksa).
    static Ucs fromOrigin(Vec3 origin) noexcept;

    // 2 NOKTADAN UCS: origin + X yonu. Z dunya Z olarak secilir; X yonu Z'ye
    // paralelse (dejenere) gecersiz doner. Y = Z x X.
    static std::optional<Ucs> fromOriginAndX(Vec3 origin, Vec3 xPoint) noexcept;

    // 3 NOKTADAN UCS: origin + X noktasi + XY duzlemini (Y tarafini) belirleyen
    // nokta. X = normalize(xPoint-origin); Z = normalize(X x (yPoint-origin));
    // Y = Z x X. Cakisan noktalarda gecersiz.
    static std::optional<Ucs> fromThreePoints(Vec3 origin, Vec3 xPoint, Vec3 yPoint) noexcept;

    // B-Rep YUZEYINDEN UCS: origin + zAxis = yuzey normali. X/Y kararli otomatik
    // secilir (normal dunya Z'ye yakinsa ref=Y, degilse ref=Z; X = ref x normal).
    static Ucs fromPlaneAndNormal(Vec3 origin, Vec3 normal) noexcept;

    // PARCA KOORDINAT SISTEMINDEN UCS: origin + uye ekseni (yerel Z) + referans
    // normali (basliklarin PARALEL oldugu duzlem — profileSectionFrame kurali).
    // X/Y, profileSectionFrame(width, height, axis) ile ayni sekilde kurulur.
    static Ucs fromPartFrame(Vec3 origin, Vec3 axis, Vec3 referenceNormal = {0.0, 0.0, 1.0}) noexcept;

    // Mevcut WorkPlane altyapisiyla birlikte calisma.
    WorkPlane toWorkPlane() const noexcept;              // u->x, v->y, normal->z
    static Ucs fromWorkPlane(const WorkPlane& plane) noexcept;

    // Dunya <-> yerel (eksen bazinda; ortonormal kabul).
    Vec3 toLocal(Vec3 worldPoint) const noexcept;
    Vec3 toWorld(Vec3 localPoint) const noexcept;

    // Gecerli sag-el ortonormal cerceve mi? (eksen uzunluklari 1, karsilikli dik)
    bool isValid(double tolerance = 1e-9) const noexcept;
};

// UCS'YE GORE VIEW: ekran saga = xAxis, ekran yukari = yAxis, izleyiciye dogru
// (normal) = zAxis; bakis yonu = -zAxis (UCS'nin XY duzlemine yukaridan bakis).
struct UcsView {
    Vec3 right{1.0, 0.0, 0.0};
    Vec3 up{0.0, 1.0, 0.0};
    Vec3 normal{0.0, 0.0, 1.0}; // izleyiciye dogru (right x up)
};

// Mevcut UCS uzerinde view (Tekla "set view to UCS / plan of UCS").
UcsView viewFromUcs(const Ucs& ucs) noexcept;

// PARCA DUZLEMLERI (Tekla "on part front/top/back/bottom plane" +
// "default views of part"). Parca cercevesi = Ucs::fromPartFrame ciktisi:
//   xAxis = genislik (width), yAxis = yukseklik (height), zAxis = eksen (axis).
// Her duzlem icin sag-el ortonormal taban uretilir (normal = right x up):
//   Front  : web'e bakis (normal = -width), right = +axis, up = +height
//   Top    : ustten bakis (normal = +height), right = +axis, up = +width
//   End    : kesit bakisi (normal = +axis), right = +width, up = +height
//   Back   : onun tersi (normal = +width)
//   Bottom : ustun tersi (normal = -height)
enum class PartPlane { Front = 0, Top, End, Back, Bottom };
UcsView viewOnPartPlane(const Ucs& partFrame, PartPlane plane) noexcept;

// Secilen yuzeye DIK view (Tekla "view perpendicular to face"): normal =
// yuzey normali, up = dunya Z (normal Z'ye paralelse dunya Y), right = up x
// normal (right x up = normal saglanir).
UcsView viewPerpendicularToFace(Vec3 origin, Vec3 faceNormal) noexcept;

// 2 NOKTADAN view: P1->P2 yatay (XY) izdusumu ekran saga, dunya Z yukari.
// (mevcut viewFromTwoPoints ile ayni kural; UcsView cinsinden.)
std::optional<UcsView> viewFromTwoPointsUcs(const Vec3& p1, const Vec3& p2) noexcept;

// 3 NOKTADAN view: origin + X noktasi + XY duzlemi noktasi -> Ucs kur, view al.
std::optional<UcsView> viewFromThreePointsUcs(Vec3 origin, Vec3 xPoint, Vec3 yPoint) noexcept;

// --- NAMED VIEWS + VIEW LIST -------------------------------------------------
// Bir adlandirilmis gorunus: kendi UCS'si + kamera merkezi. Gerektiginde
// zoom/depth alanlari eklenebilir (MMW8 kaliciligi ayri faz).
struct NamedView {
    std::wstring name;
    Ucs ucs;        // gorunusun koordinat sistemi
    Vec3 origin{};  // kamera merkezi (genelde ucs.origin ile ayni)
};

class NamedViewList {
public:
    // Ekler; ayni isim varsa UZERINE yazar (Tekla "rename/overwrite" davranisi).
    // Bos isim reddedilir (false). true = eklendi/yeniden yazildi.
    bool add(NamedView view) noexcept;
    // Isimle siler; bulunamazsa false.
    bool remove(const std::wstring& name) noexcept;
    void clear() noexcept;
    // Isimle arar (nullptr = yok). Buyuk/kucuk harf DUYARLI.
    const NamedView* find(const std::wstring& name) const noexcept;
    bool contains(const std::wstring& name) const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    // Isim sirali, deterministik liste (UI View List icin).
    std::vector<std::wstring> names() const;

private:
    std::vector<NamedView> views_;
};

} // namespace mm
