#include "model_maker/assembly.hpp"

#include <algorithm>
#include <utility>

namespace mm {

std::size_t AssemblyModel::addAssembly(Assembly assembly) noexcept {
    assemblies_.push_back(std::move(assembly));
    return assemblies_.size() - 1;
}

bool AssemblyModel::removeAssembly(const std::wstring& name) noexcept {
    const auto it = std::find_if(assemblies_.begin(), assemblies_.end(),
                                 [&](const Assembly& a) { return a.name == name; });
    if (it == assemblies_.end()) return false;
    assemblies_.erase(it);
    return true;
}

const Assembly* AssemblyModel::findAssembly(const std::wstring& name) const noexcept {
    for (const auto& a : assemblies_)
        if (a.name == name) return &a;
    return nullptr;
}

bool AssemblyModel::addPart(const std::wstring& name, EntityId partId) noexcept {
    for (auto& a : assemblies_) {
        if (a.name != name) continue;
        if (partId == kInvalidEntityId) return false;
        if (std::find(a.parts.begin(), a.parts.end(), partId) != a.parts.end()) return true; // yinelenmez
        a.parts.push_back(partId);
        return true;
    }
    return false;
}

bool AssemblyModel::removePart(const std::wstring& name, EntityId partId) noexcept {
    for (auto& a : assemblies_) {
        if (a.name != name) continue;
        const auto it = std::find(a.parts.begin(), a.parts.end(), partId);
        if (it == a.parts.end()) return false;
        a.parts.erase(it);
        return true;
    }
    return false;
}

bool AssemblyModel::setMainPart(const std::wstring& name, EntityId partId) noexcept {
    for (auto& a : assemblies_) {
        if (a.name != name) continue;
        a.mainPart = partId;
        return true;
    }
    return false;
}

std::vector<std::wstring> AssemblyModel::assembliesContaining(EntityId partId) const {
    std::vector<std::wstring> result;
    for (const auto& a : assemblies_)
        if (std::find(a.parts.begin(), a.parts.end(), partId) != a.parts.end())
            result.push_back(a.name);
    return result;
}

Vec2 orthoProject(const Vec3& p, OrthoView view) noexcept {
    StandardView standard = StandardView::Top;
    switch (view) {
    case OrthoView::Top: standard = StandardView::Top; break;
    case OrthoView::Front: standard = StandardView::Front; break;
    case OrthoView::Right: standard = StandardView::Right; break;
    case OrthoView::Isometric: standard = StandardView::Isometric; break;
    }
    const auto R = Camera::standardViewRotation(standard); // row-major world->camera
    // Ekran (cizim) x = kamera saga (row0), y = kamera yukari (row1).
    return {R[0] * p.x + R[1] * p.y + R[2] * p.z,
            R[3] * p.x + R[4] * p.y + R[5] * p.z};
}

std::size_t PaperSpace::addView(DrawingView view) noexcept {
    views_.push_back(std::move(view));
    return views_.size() - 1;
}

bool PaperSpace::removeView(std::size_t index) noexcept {
    if (index >= views_.size()) return false;
    views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

} // namespace mm
