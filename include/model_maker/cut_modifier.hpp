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

enum class CutKind { PlaneCut, Fitting, PolygonCut, BooleanCut };

// Kalici kesme modifier'i.
struct CutModifier {
    CutKind kind{CutKind::PlaneCut};
    EntityId target{kInvalidEntityId};   // kesilen parca (kimlik KORUNUR)
    Vec3 planeOrigin{};                  // PlaneCut/Fitting: duzlem origin
    Vec3 planeNormal{0.0, 0.0, 1.0};     // PlaneCut/Fitting: duzlem normali
    bool keepPositive{true};             // PlaneCut: normalin + tarafi korunur
    std::vector<Vec3> polygon;           // PolygonCut: duzlemde cokgen (>=3 nokta)
    EntityId tool{kInvalidEntityId};     // BooleanCut: diger parca
    bool enabled{true};
    std::string name;   // UTF-8 (kalicilikta taşınabilir; Qt QString::fromUtf8 ile)
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
// Kose kumesinin duzleme gore taraf siniflandirmasi (PlaneCut on karari).
std::vector<int> classifyVerticesAgainstPlane(const std::vector<Vec3>& vertices,
                                              const Vec3& origin, const Vec3& normal,
                                              double tol = 1e-9) noexcept;

} // namespace mm
