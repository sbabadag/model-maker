#pragma once

// MAGNETIC FRAME / COMPONENT LOCAL FRAME (Phase R2d-1) — component'in sahip
// oldugu alt nesnelerin (Plate, BoltGroup, HoleModifier, Weld, Construction
// Object) konumlarinin turetildigi RIJIT cerceve.
//
// MagneticFrame tasindiginda alt nesnelerin DUNYA koordinatlari SAF FONKSIYON
// ile YENIDEN TURETILIR — mevcut B-Rep'leri oteleyerek parametrik yeniden
// hesaplamayi TAKLIT ETMEZ. Cerceve ortonormal sag-elli bir rotasyon (basis)
// + origin'dir; lokal vektorler/normaller basis ile (origin OLMADAN), lokal
// noktalar origin + basis ile dunyaya tasinir.

#include "model_maker/reference_geometry.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mm {

// Alt nesnenin cerceve icindeki LOKAL yerlesimi (rigid transform). u/v/w
// BIRIM ve karsilikli DIK olmalidir (sag-elli: u x v == w).
struct LocalPlacement {
    Vec3 origin{};             // lokal origin (cerceve koordinatlarinda)
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};
    Vec3 w{0.0, 0.0, 1.0};
};

// MagneticFrame'in sahip oldugu alt nesne (kararli kimlik + lokal yerlesim).
struct MagneticChild {
    std::string id;            // kararli kimlik (Plate, BoltGroup, ...)
    LocalPlacement local;
};

// ComponentLocalFrame: rijit magnetic cerceve + alt nesneler.
struct ComponentLocalFrame {
    MemberLocalFrame frame;    // magnetic frame (dunya)
    std::vector<MagneticChild> children;
};

// LocalPlacement'in RIJIT (birim, karsilikli dik, sag-elli) oldugunu dogrular.
bool isRigidLocalPlacement(const LocalPlacement& p, double tol = 1e-9) noexcept;

// ComponentLocalFrame'in GECERLI oldugunu dogrular: magnetic cerceve ortonormal
// sag-elli VE tum alt nesne lokal yerlesimleri rijit.
bool isValidComponentLocalFrame(const ComponentLocalFrame& clf, double tol = 1e-9) noexcept;

// Lokal vektor/NORMAL -> dunya (basis ile, origin OLMADAN). Ortonormal basis
// saf rotasyon oldugundan normal de vektor gibi ayni kurala doner.
Vec3 magneticTransformVec(const MemberLocalFrame& frame, const Vec3& local) noexcept;

// Lokal nokta -> dunya (origin + basis).
Vec3 magneticTransformPoint(const MemberLocalFrame& frame, const Vec3& local) noexcept;

// Lokal cerceve -> dunya cercevesi (parent.frame altinda childLocal).
MemberLocalFrame magneticTransformFrame(const MemberLocalFrame& parent,
                                        const MemberLocalFrame& childLocal) noexcept;

// Alt nesnelerin DUNYA yerlesimlerini yeniden turet (id ile esle).
// Magnetic cerceve dejenere ise BOS doner (fail-closed); rijit olmayan bir alt
// nesnenin lokal yerlesimi ATLANIR (copluk uretilmez).
std::vector<std::pair<std::string, MemberLocalFrame>> computeChildWorldFrames(
    const ComponentLocalFrame& clf);

} // namespace mm
