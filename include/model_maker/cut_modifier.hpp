#pragma once

// KESME NESNELERI (P3) — kalici + duzenlenebilir modifier modeli.
//
// Kesme islemleri (Plane Cut / Fitting / Polygon Cut / Boolean Cut) hedef
// parcanin EntityId'sini KORUYAN, kalici ve duzenlenebilir "modifier" kayitlari
// olarak tutulur. Modifier'in KENDI geometrisi (duzlem/cokgen) ve siniflandirma
// matematigi burada (windows.h yok, Linux'ta test edilir); gercek B-Rep boolean
// uygulamasi OCC katmaninda (Windows) yapilir ve bu kayitlari girdi olarak
// tuketir.
//
// Tasarim: CutModifier indeks-bazli (grid gibi) Document'ta tutulur; hedef ve
// arac PARCALAR EntityId ile referanslanir — kesme parcanin kimligini degistirmez,
// yalniz geometri surumunu ilerletir (recordGeometryDelta / restoreActive).

#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mm {

enum class CutKind { PlaneCut, Fitting, PolygonCut, BooleanCut, LineCut, Hole };

// Kalici kesme modifier'i.
struct CutModifier {
    CutKind kind{CutKind::PlaneCut};
    EntityId target{kInvalidEntityId};   // kesilen parca (kimlik KORUNUR)
    Vec3 planeOrigin{};                  // PlaneCut/Fitting: duzlem origin
    Vec3 planeNormal{0.0, 0.0, 1.0};     // PlaneCut/Fitting: duzlem normali;
                                         // LineCut: cizginin cizildigi duzlemin
                                         // normali (gorunum/is duzlemi).
    bool keepPositive{true};             // PlaneCut/LineCut: normalin + tarafi korunur
    std::vector<Vec3> polygon;           // PolygonCut: duzlemde cokgen (>=3 nokta);
                                         // LineCut: {cizgiBas, cizgiSon} (2 nokta)
    EntityId tool{kInvalidEntityId};     // BooleanCut: diger parca
    // KAYNAK VARLIK (P6): PlaneCut/Fitting icin konstrüksiyon duzleminin,
    // LineCut icin cizginin, PolygonCut icin 3DFACE'in EntityId'si. Kaynak
    // gecerliyken modifier GEOMETRISI KOPYALANMAZ — kaynak varligin GUNCEL
    // geometrisinden cozumlenir (resolveCutModifierSource). Boylece kaynak
    // tasininca kesme onu izler, kaynak silinince kesme kaskad kaldirilir ve
    // ayri bir "kopya kesme objesi" cizilmez. kInvalidEntityId = eski/kopiya
    // davranis (geometri mod.planeOrigin/planeNormal/polygon'da sabittir).
    EntityId source{kInvalidEntityId};
    bool enabled{true};
    std::string name;   // UTF-8 (kalıcılıkta taşınabilir; Qt QString::fromUtf8 ile)
    // YÖN (P8): kesme yönü ters. Alt+sol tık (kesme nesnesi üzerinde) ile
    // değiştirilir. PlaneCut/LineCut: keepPositive terslenir. Fitting: OTOMATIK
    // taraf terslenir (uzun/kisa taraf secimi yer degistirir). PolygonCut /
    // BooleanCut: yönsüz (through-cut / boolean) — etkisizdir.
    bool invertSide{false};
    // DELIK (Phase B1, CutKind::Hole): delik capi (mm). Hole icin eksen =
    // planeNormal (birim), merkez = planeOrigin; silindir katiyi boydan boya
    // keser (through-cut). Diger turlerde 0 (kullanilmaz).
    double diameter{0.0};
    // DELIK DERINLIGI (CutKind::Hole): sinirli delik boyu (mm, eksen boyunca).
    // 0 = through-cut (eski davranis, geriye uyumlu). >0 ise silindir yalniz
    // bu uzunlukta keser — ornegin End Plate bulonu kolonun YALNIZ on flansini
    // deler, arka flansa ulasmaz. Diger turlerde 0.
    double depth{0.0};
    // YUVALI / OVAL DELIK (Tekla "Slotted holes", CutKind::Hole): her iki deger
    // delik capina EKLENEN uzunluktur (mm). slotAlong = slotDir boyunca, slotAcross
    // = slotDir'e (ve eksene) dik yonde. Ikisi de 0 = yuvarlak delik (eski davranis).
    // Kesit: kenar yaricapi min(en,boy)/2 olan yuvarlatilmis dikdortgen (tek uzun
    // yonde "stadyum" yuva). slotDir birim ve eksene dik olmali; degilse delik
    // yuvarlak kesilir (guvenli no-op).
    double slotAlong{0.0};
    double slotAcross{0.0};
    Vec3 slotDir{0.0, 0.0, 0.0};
    // BULON SAHIPLIGI (Phase B1): bu modifier'i ureten BoltGroup'un EntityId'si.
    // Hole modifier'lari bir bulon grubuna aittir; bulon silinince/taşıninca
    // yalnizca bu sahipligi tasiyan modifier'lar kaldirilir/yeniden uretilir.
    // Diger kesme turlerinde kInvalidEntityId.
    EntityId owner{kInvalidEntityId};
    // GIZLI (motor eleman): true ise modifier COMPONENT ic mekanizmasidir
    // (End Plate fitting'i gibi) — renderer CIZMEZ, hit-test SECMEZ, kullanici
    // Alt+tik ile yon ceviremez/silemez. Kesme yine de uygulanir (recompute).
    bool hidden{false};
    bool operator==(const CutModifier&) const = default;
};

// --- DUZLEM GEOMETRISI (saf matematik) --------------------------------------
// Nokta -> duzlem isaretli uzaklik (mm). normal birim olmali; degilse normalize.
double signedDistanceToPlane(const Vec3& origin, const Vec3& normal, const Vec3& p) noexcept;
// -1 = normalin ARKASI (-taraf), 0 = uzerinde, +1 = normalin ONU (+taraf).
int planeSide(const Vec3& origin, const Vec3& normal, const Vec3& p, double tol = 1e-9) noexcept;
// Noktayi duzleme iz dusur.
Vec3 projectToPlane(const Vec3& origin, const Vec3& normal, const Vec3& p) noexcept;

// --- COKGEN GEOMETRISI ------------------------------------------------------
// Newell yontemiyle cokgen normali (birim). Degenerese (alan ~0) nullopt.
std::optional<Vec3> polygonNormal(const std::vector<Vec3>& points) noexcept;
// Nokta cokgenin icinde mi? (cokgen duzlemine iz dusurulur, 2B ray-cast).
// normal nullopt ise polygonNormal'dan turetilir.
bool pointInPolygon(const std::vector<Vec3>& polygon, const std::optional<Vec3>& normal,
                    const Vec3& p) noexcept;

// --- KESME YARDIMCILARI -----------------------------------------------------
// Dogru parcasinin duzlemle kesisimi; paralel/kesismiyorsa nullopt.
std::optional<Vec3> segmentPlaneIntersection(const Vec3& a, const Vec3& b,
                                             const Vec3& origin, const Vec3& normal) noexcept;
// Fitting (uc kesme): parca ekseni [from,to] duzlemde kac mm'de kesilir?
// from'dan kesisime olan mesafe (0..1 orani degil, mm cinsinden uzunluk).
std::optional<double> fittingTrimDistance(const Vec3& from, const Vec3& to,
                                          const Vec3& origin, const Vec3& normal) noexcept;
// Fitting (uc kesme) — Tekla "Fit part end" OTOMATIK taraf secimi.
// Parca ekseni [axisFrom, axisTo] duzlemi KESIYORSA kisa taraf kesilir:
// eksenin iki ucunun duzleme isaretli uzakligindan UZUN olan taraf korunur,
// kisa olan kaldirilir. Donus, cutSolidByPlane'a verilecek keepPositive
// degeridir (korunan taraf normalin hangi yonunde). Duzlem ekseni KESMIYORSA
// (iki uc ayni tarafta = uzatma durumu; veya eksen duzleme yatik/uzerinde)
// nullopt doner — prototip kapsami yalniz kesen duzlemdir.
std::optional<bool> fittingKeepPositive(const Vec3& axisFrom, const Vec3& axisTo,
                                        const Vec3& origin, const Vec3& normal) noexcept;
// Line Cut (Tekla "cut with a line"): kesme duzlemi cizgiyi ICERIR ve cizginin
// cizildigi (gorunum/is) duzlemine DIKTIR. Donus = kesme duzlemi normali (birim)
// = normalize(cizgiYonu x referenceNormal). referenceNormal = cizginin cizildigi
// duzlemin normali (aktif is duzlemi normali). Dejenere (cizgi ~0 veya cizgiYonu
// ∥ referenceNormal) ise nullopt.
std::optional<Vec3> lineCutPlaneNormal(const Vec3& from, const Vec3& to,
                                       const Vec3& referenceNormal) noexcept;
// Kose kumesinin duzleme gore taraf siniflandirmasi (PlaneCut on karari).
std::vector<int> classifyVerticesAgainstPlane(const std::vector<Vec3>& vertices,
                                              const Vec3& origin, const Vec3& normal,
                                              double tol = 1e-9) noexcept;

} // namespace mm
