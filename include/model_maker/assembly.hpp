#pragma once

// ASSEMBLY & PAPER SPACE (P5) — temel mimari.
//
// - Part/Assembly iliskileri: Assembly (isim + main part + uye parcalar);
//   parcalar EntityId ile referanslanir (kimlik korunur). AssemblyModel
//   uyelik/main-part/ters-sorgu (bir parca hangi assembly'lerde) yonetir.
// - Model tabanli ORTOGRAFIK cizim gorunusu: 3B nokta -> 2B cizim. Projeksiyon
//   Camera::standardViewRotation ile AYNI rotasyonlari kullanir (TEK KAYNAK —
//   ekrandaki Top/Front/Right/Iso gorunusu ile cizim gorunusu birebir tutarli).
// - Paper Space mimari temeli: DrawingView (assembly + ortografik gorunus +
//   kagit yerlesimi + olcek) + PaperSpace (gorunus listesi).

#include "model_maker/camera.hpp"
#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mm {

struct Assembly {
    std::wstring name;
    EntityId mainPart{kInvalidEntityId};
    std::vector<EntityId> parts; // uye parcalar (main part dahil olabilir)
};

class AssemblyModel {
public:
    std::size_t addAssembly(Assembly assembly) noexcept;
    bool removeAssembly(const std::wstring& name) noexcept;
    const Assembly* findAssembly(const std::wstring& name) const noexcept;
    const std::vector<Assembly>& assemblies() const noexcept { return assemblies_; }
    std::size_t size() const noexcept { return assemblies_.size(); }
    // Part/Assembly iliskileri.
    bool addPart(const std::wstring& name, EntityId partId) noexcept;   // yinelenmez
    bool removePart(const std::wstring& name, EntityId partId) noexcept;
    bool setMainPart(const std::wstring& name, EntityId partId) noexcept;
    // Bir parca hangi assembly'lerde? (isim listesi, eklenme sirasinda).
    std::vector<std::wstring> assembliesContaining(EntityId partId) const;

private:
    std::vector<Assembly> assemblies_;
};

// Orto projeksiyon gorunusleri (cizim kagidi). Interactive 3B standart
// gorunusleriyle (Camera::setView) AYNI yonler.
enum class OrthoView { Top, Front, Right, Isometric };

// 3B nokta -> 2B cizim (ortografik, perspektifsiz). Camera::standardViewRotation
// ile AYNI rotasyon (TEK KAYNAK): ekrandaki standart gorunus ile cizim
// gorunusu birebir tutarli. Screen x = kamera saga, y = kamera yukari.
Vec2 orthoProject(const Vec3& p, OrthoView view) noexcept;

// Paper Space gorunusu.
struct DrawingView {
    std::wstring assemblyName;
    OrthoView view{OrthoView::Top};
    Vec2 placement{}; // kagit uzerinde gorunus origin konumu (mm)
    double scale{1.0};
};

class PaperSpace {
public:
    std::size_t addView(DrawingView view) noexcept;
    bool removeView(std::size_t index) noexcept;
    const std::vector<DrawingView>& views() const noexcept { return views_; }
    std::size_t size() const noexcept { return views_.size(); }

private:
    std::vector<DrawingView> views_;
};

} // namespace mm
