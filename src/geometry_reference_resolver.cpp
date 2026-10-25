#include "model_maker/geometry_reference_resolver.hpp"

#include <cmath>

namespace mm {

namespace {

double vdot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double vnorm(const Vec3& a) noexcept { return std::sqrt(vdot(a, a)); }
Vec3 vneg(const Vec3& a) noexcept { return {a.x * -1.0, a.y * -1.0, a.z * -1.0}; }

// Uye cercevesini dogrular; dejenere (ortonormal/sag-elli degil) ise cozume
// gecilmez (fail-closed: gecersiz cerceveden yanlis duzlem turetilmez).
bool frameValid(const MemberLocalFrame& fr) noexcept {
    return isOrthonormalRightHanded(fr, 1e-9);
}

} // namespace

ResolvedGeometry BasicGeometryReferenceResolver::resolve(const GeometryReference& ref) const {
    ResolvedGeometry out;

    // FAIL-CLOSED: bilinmeyen tur ve kayip kaynak (source == kInvalidEntityId)
    // TUM turlerde UNRESOLVED'dur. Construction/Component duzlemi dahil her
    // referans kalici bir kaynak EntityId'si ile tanimlanir; explicitPlane
    // yalnizca runtime cozum degeri (cache) — kimlik DEGILDIR. Kaynak yoksa
    // referans KAYIPTIR ve sessizce cozulmez.
    if (ref.kind == GeometryReferenceKind::None) return out;
    if (ref.source == kInvalidEntityId) return out;

    switch (ref.kind) {
        case GeometryReferenceKind::ConstructionPlane:
        case GeometryReferenceKind::ComponentPlane: {
            if (!ref.explicitPlane) return out;  // duzlem degeri yok
            const double n = vnorm(ref.explicitPlane->normal);
            if (n < 1e-9) return out;  // dejenere (sifir) normal
            out.resolved = true;
            out.status = ResolutionStatus::Resolved;
            out.plane.origin = ref.explicitPlane->origin;
            out.plane.normal = ref.explicitPlane->normal * (1.0 / n);  // birim normal
            return out;
        }

        case GeometryReferenceKind::MemberFrame: {
            const auto it = members_.find(ref.source);
            if (it == members_.end()) return out;
            const MemberLocalFrame fr = ref.localFrame.value_or(it->second.frame);
            if (!frameValid(fr)) return out;  // dejenere cerceve
            out.resolved = true;
            out.status = ResolutionStatus::Resolved;
            out.frame = fr;
            out.plane.origin = fr.origin;
            out.plane.normal = fr.w;  // kimlik duzlemi: origin + w
            return out;
        }

        case GeometryReferenceKind::MemberStart: {
            const auto it = members_.find(ref.source);
            if (it == members_.end()) return out;
            const MemberLocalFrame fr = ref.localFrame.value_or(it->second.frame);
            if (!frameValid(fr)) return out;
            out.resolved = true;
            out.status = ResolutionStatus::Resolved;
            out.frame = fr;
            out.plane.origin = fr.origin;
            out.plane.normal = vneg(fr.w);  // -w
            return out;
        }

        case GeometryReferenceKind::MemberEnd: {
            const auto it = members_.find(ref.source);
            if (it == members_.end()) return out;
            const MemberRecord& m = it->second;
            const MemberLocalFrame fr = ref.localFrame.value_or(m.frame);
            if (!frameValid(fr)) return out;
            out.resolved = true;
            out.status = ResolutionStatus::Resolved;
            out.frame = fr;
            out.plane.origin = fr.origin + fr.w * m.length;
            out.plane.normal = fr.w;  // +w
            return out;
        }

        case GeometryReferenceKind::SemanticFace: {
            if (ref.faceRole == SemanticFaceRole::None) return out;
            const auto it = members_.find(ref.source);
            if (it == members_.end()) return out;
            const MemberRecord& m = it->second;
            const MemberLocalFrame fr = ref.localFrame.value_or(m.frame);
            if (!frameValid(fr)) return out;  // dejenere cerceve -> cozum yok
            const auto plane = resolveSemanticFacePlane(fr, m.dims, m.length, ref.faceRole);
            if (!plane) return out;
            out.resolved = true;
            out.status = ResolutionStatus::Resolved;
            out.frame = fr;
            out.plane = *plane;
            return out;
        }

        default:
            return out;  // bilinmeyen -> UNRESOLVED
    }
}

} // namespace mm
